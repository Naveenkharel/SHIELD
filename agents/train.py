"""
agents/train.py

Main training entry point for SHIELD's QMIX agents.

Wires together:
    - TCPEnvClient   (talks to Keshav's ns-3 bridge server)
    - QMIXTrainer    (QNetwork + MixingNetwork + ReplayBuffer + train_step)

Handles:
    - epsilon-greedy exploration with decay over episodes
    - per-episode logging (console + CSV file)
    - periodic checkpointing of trained weights
    - graceful shutdown on connection errors / Ctrl+C

Usage:
    python train.py --host <keshav_ip_or_localhost> --port 9999 --episodes 500

NOTE on imports: matches the flat-import style used in qmix_trainer.py
(i.e. this script is meant to be run FROM INSIDE the agents/ folder, not
as a package from the project root). If you later restructure to run
from the project root, change these three imports to `from agents.X
import Y` and add an agents/__init__.py.
"""

import argparse
import csv
import os
import time

import numpy as np
import torch

from qmix_trainer import QMIXTrainer
from tcp_env_client import TCPEnvClient


def compute_epsilon(episode: int, total_episodes: int, eps_start: float = 1.0,
                     eps_end: float = 0.05, decay_fraction: float = 0.7) -> float:
    """
    Linearly decay epsilon from eps_start to eps_end over the first
    `decay_fraction` of total training episodes, then hold at eps_end.

    Example: with decay_fraction=0.7 and total_episodes=500, epsilon
    reaches eps_end by episode 350, then stays there for the remaining
    150 episodes (mostly-greedy fine-tuning phase).
    """
    decay_episodes = int(total_episodes * decay_fraction)
    if decay_episodes <= 0:
        return eps_end
    progress = min(episode / decay_episodes, 1.0)
    return eps_start + progress * (eps_end - eps_start)


def run_training(args):
    print(f"Connecting to ns-3 bridge server at {args.host}:{args.port} ...")
    env = TCPEnvClient(host=args.host, port=args.port,
                        n_agents=args.n_agents, obs_dim=args.obs_dim)
    print("Connected.")

    trainer = QMIXTrainer(
        n_agents=args.n_agents,
        obs_dim=args.obs_dim,
        n_actions=args.n_actions,
        state_dim=args.n_agents * args.obs_dim,
        gamma=args.gamma,
        lr=args.lr,
        buffer_capacity=args.buffer_capacity,
        target_update_freq=args.target_update_freq,
        device=args.device,
    )

    os.makedirs(args.checkpoint_dir, exist_ok=True)
    log_path = os.path.join(args.checkpoint_dir, "training_log.csv")
    log_file = open(log_path, "w", newline="")
    log_writer = csv.writer(log_file)
    log_writer.writerow(["episode", "total_reward", "avg_loss", "epsilon", "steps", "duration_sec"])

    try:
        for episode in range(1, args.episodes + 1):
            epsilon = compute_epsilon(episode, args.episodes,
                                       args.eps_start, args.eps_end, args.eps_decay_fraction)

            obs = env.reset()
            done = False
            total_reward = 0.0
            losses = []
            step_count = 0
            episode_start = time.time()

            while not done:
                actions = trainer.select_actions_for_all_agents(obs, epsilon=epsilon)
                next_obs, reward, done, info = env.step(actions)

                state = obs.flatten()
                next_state = next_obs.flatten()
                trainer.replay_buffer.push(obs, actions, reward, next_obs, state, next_state, done)

                loss = trainer.train_step(batch_size=args.batch_size)
                if loss is not None:
                    losses.append(loss)

                obs = next_obs
                total_reward += reward
                step_count += 1

                # Safety cap in case an episode never sets done=True.
                if step_count >= args.max_steps_per_episode:
                    break

            duration = time.time() - episode_start
            avg_loss = float(np.mean(losses)) if losses else None

            print(f"Episode {episode}/{args.episodes} | "
                  f"reward={total_reward:.3f} | "
                  f"avg_loss={avg_loss if avg_loss is None else round(avg_loss, 4)} | "
                  f"epsilon={epsilon:.3f} | steps={step_count} | {duration:.1f}s")

            log_writer.writerow([episode, total_reward, avg_loss, epsilon, step_count, round(duration, 2)])
            log_file.flush()

            if episode % args.checkpoint_every == 0:
                save_checkpoint(trainer, args.checkpoint_dir, f"checkpoint_ep{episode}.pt")

        # Always save a final checkpoint at the end of training.
        save_checkpoint(trainer, args.checkpoint_dir, "checkpoint_final.pt")

    except KeyboardInterrupt:
        print("\nTraining interrupted by user. Saving current progress before exiting...")
        save_checkpoint(trainer, args.checkpoint_dir, "checkpoint_interrupted.pt")

    except ConnectionError as e:
        print(f"\nLost connection to ns-3 server: {e}")
        print("Saving current progress before exiting...")
        save_checkpoint(trainer, args.checkpoint_dir, "checkpoint_on_disconnect.pt")

    finally:
        log_file.close()
        env.close()
        print("Training session ended, connection closed.")


def save_checkpoint(trainer: QMIXTrainer, checkpoint_dir: str, filename: str):
    """
    Save the online QNetwork and MixingNetwork weights (the target
    networks are just periodic copies of these, so they don't need to be
    saved separately). This is what gets deployed at inference time --
    corresponds to "D2 Trained Policies" in the class diagram.
    """
    path = os.path.join(checkpoint_dir, filename)
    torch.save({
        "q_network_state_dict": trainer.q_network.state_dict(),
        "mixing_network_state_dict": trainer.mixing_network.state_dict(),
    }, path)
    print(f"  -> saved checkpoint: {path}")


def parse_args():
    parser = argparse.ArgumentParser(description="Train SHIELD's QMIX agents against the ns-3 bridge.")

    # Connection
    parser.add_argument("--host", type=str, required=True, help="ns-3 server host (IP or 'localhost' if tunneled)")
    parser.add_argument("--port", type=int, default=9999)

    # Environment / agent dimensions
    parser.add_argument("--n_agents", type=int, default=14)
    parser.add_argument("--obs_dim", type=int, default=12)
    parser.add_argument("--n_actions", type=int, default=5)

    # Training hyperparameters
    parser.add_argument("--episodes", type=int, default=500)
    parser.add_argument("--max_steps_per_episode", type=int, default=500,
                         help="Safety cap in case the server never returns done=True.")
    parser.add_argument("--batch_size", type=int, default=32)
    parser.add_argument("--gamma", type=float, default=0.99)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--buffer_capacity", type=int, default=50000)
    parser.add_argument("--target_update_freq", type=int, default=200)

    # Epsilon-greedy exploration schedule
    parser.add_argument("--eps_start", type=float, default=1.0)
    parser.add_argument("--eps_end", type=float, default=0.05)
    parser.add_argument("--eps_decay_fraction", type=float, default=0.7,
                         help="Fraction of total episodes over which epsilon decays.")

    # Checkpointing / device
    parser.add_argument("--checkpoint_dir", type=str, default="checkpoints")
    parser.add_argument("--checkpoint_every", type=int, default=50)
    parser.add_argument("--device", type=str, default="cpu", choices=["cpu", "cuda"])

    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    run_training(args)