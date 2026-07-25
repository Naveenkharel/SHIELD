"""
agents/qmix_trainer.py

QMIX Trainer: wires together QNetwork, MixingNetwork, and ReplayBuffer
into a full training pipeline.

Holds FOUR networks total:
    - online QNetwork    (actively trained, shared across all agents)
    - target QNetwork    (frozen copy, updated periodically)
    - online MixingNetwork
    - target MixingNetwork

Exposes:
    - select_action(obs, epsilon)  -> epsilon-greedy action for ONE agent
    - train_step(batch_size)       -> one gradient update using a sampled batch
    - update_target_networks()     -> hard-copy online weights into target nets

This module is environment-agnostic: it does not know or care whether
observations come from MockShieldEnv or the real ns-3 Gym bridge. It only
consumes (obs, action, reward, next_obs, state, next_state, done) tuples
via the ReplayBuffer.
"""

import random

import torch
import torch.nn as nn
import torch.optim as optim

from q_network import QNetwork
from mixing_network import MixingNetwork
from replay_buffer import ReplayBuffer


class QMIXTrainer:
    def __init__(
        self,
        n_agents: int = 14,
        obs_dim: int = 12,
        n_actions: int = 5,
        state_dim: int = 168,
        gamma: float = 0.99,
        lr: float = 1e-3,
        buffer_capacity: int = 50000,
        target_update_freq: int = 200,
        device: str = "cpu",
    ):
        """
        Args:
            n_agents: number of router agents (14 for NSFNET).
            obs_dim: per-agent observation size (12).
            n_actions: number of discrete actions (5).
            state_dim: global state size (n_agents * obs_dim = 168).
            gamma: discount factor for future rewards.
            lr: learning rate for the optimizer.
            buffer_capacity: max size of the replay buffer.
            target_update_freq: how many train_step() calls between hard
                                 target-network updates.
            device: "cpu" or "cuda".
        """
        self.n_agents = n_agents
        self.obs_dim = obs_dim
        self.n_actions = n_actions
        self.state_dim = state_dim
        self.gamma = gamma
        self.target_update_freq = target_update_freq
        self.device = torch.device(device)

        # --- Online networks (actively trained) ---
        self.q_network = QNetwork(obs_dim, n_actions).to(self.device)
        self.mixing_network = MixingNetwork(n_agents, state_dim).to(self.device)

        # --- Target networks (frozen copies, used for stable TD targets) ---
        self.target_q_network = QNetwork(obs_dim, n_actions).to(self.device)
        self.target_mixing_network = MixingNetwork(n_agents, state_dim).to(self.device)

        # Initialize target nets with the same weights as the online nets.
        self.target_q_network.load_state_dict(self.q_network.state_dict())
        self.target_mixing_network.load_state_dict(self.mixing_network.state_dict())

        # Target networks are never trained directly via backprop -- only
        # ever updated by copying online weights (see update_target_networks).
        self.target_q_network.eval()
        self.target_mixing_network.eval()

        # --- Optimizer -- trains online QNetwork + online MixingNetwork jointly ---
        params = list(self.q_network.parameters()) + list(self.mixing_network.parameters())
        self.optimizer = optim.Adam(params, lr=lr)

        # --- Replay buffer ---
        self.replay_buffer = ReplayBuffer(capacity=buffer_capacity)

        self._train_step_count = 0

    # ------------------------------------------------------------------
    # Acting in the environment
    # ------------------------------------------------------------------
    def select_action(self, obs, epsilon: float) -> int:
        """
        Epsilon-greedy action selection for ONE agent.

        Args:
            obs: array-like, shape (obs_dim,) -- a single agent's local observation.
            epsilon: probability of picking a random action instead of the
                     greedy best one (exploration).

        Returns:
            action: int, index in [0, n_actions).
        """
        if random.random() < epsilon:
            return random.randrange(self.n_actions)

        with torch.no_grad():
            obs_tensor = torch.tensor(obs, dtype=torch.float32, device=self.device)
            q_values = self.q_network(obs_tensor)  # shape (n_actions,)
            action = torch.argmax(q_values).item()
        return action

    def select_actions_for_all_agents(self, obs_all, epsilon: float):
        """
        Convenience wrapper: epsilon-greedy action selection for ALL agents
        at once, since weights are shared (Step 3 decision).

        Args:
            obs_all: array-like, shape (n_agents, obs_dim).
            epsilon: exploration probability, same for all agents.

        Returns:
            actions: list of ints, length n_agents.
        """
        return [self.select_action(obs_all[i], epsilon) for i in range(self.n_agents)]

    # ------------------------------------------------------------------
    # Training
    # ------------------------------------------------------------------
    def train_step(self, batch_size: int = 32):
        """
        Perform one QMIX training update using a random batch from the
        replay buffer.

        Returns:
            loss value (float), or None if the buffer isn't ready yet.
        """
        if not self.replay_buffer.is_ready(batch_size):
            return None

        batch = self.replay_buffer.sample(batch_size)
        obs = batch["obs"].to(self.device)              # (batch, n_agents, obs_dim)
        actions = batch["actions"].to(self.device)       # (batch, n_agents)
        reward = batch["reward"].to(self.device)         # (batch, 1)
        next_obs = batch["next_obs"].to(self.device)     # (batch, n_agents, obs_dim)
        state = batch["state"].to(self.device)            # (batch, state_dim)
        next_state = batch["next_state"].to(self.device)  # (batch, state_dim)
        done = batch["done"].to(self.device)               # (batch, 1)

        batch_len = obs.size(0)

        # ---- Step 1: current Q-values for the actions actually taken ----
        # Flatten (batch, n_agents, obs_dim) -> (batch * n_agents, obs_dim) so
        # the SHARED QNetwork can process every agent's obs in one forward pass.
        obs_flat = obs.view(batch_len * self.n_agents, self.obs_dim)
        q_all_actions = self.q_network(obs_flat)  # (batch * n_agents, n_actions)
        q_all_actions = q_all_actions.view(batch_len, self.n_agents, self.n_actions)

        # Select the Q-value corresponding to the action actually taken by each agent.
        actions_idx = actions.unsqueeze(-1)  # (batch, n_agents, 1)
        chosen_q = torch.gather(q_all_actions, dim=2, index=actions_idx).squeeze(-1)
        # chosen_q shape: (batch, n_agents)

        q_tot = self.mixing_network(chosen_q, state)  # (batch, 1)

        # ---- Step 2: target Q-values (best possible next action, via target nets) ----
        with torch.no_grad():
            next_obs_flat = next_obs.view(batch_len * self.n_agents, self.obs_dim)
            target_q_all_actions = self.target_q_network(next_obs_flat)
            target_q_all_actions = target_q_all_actions.view(
                batch_len, self.n_agents, self.n_actions
            )
            # Best possible action per agent, according to the FROZEN target network.
            target_max_q, _ = target_q_all_actions.max(dim=2)  # (batch, n_agents)

            target_q_tot = self.target_mixing_network(target_max_q, next_state)  # (batch, 1)

            # ---- Step 3: TD target ----
            td_target = reward + self.gamma * target_q_tot * (1 - done)

        # ---- Step 4: loss + backprop ----
        loss = nn.functional.mse_loss(q_tot, td_target)

        self.optimizer.zero_grad()
        loss.backward()
        self.optimizer.step()

        # ---- Step 5: periodically hard-update target networks ----
        self._train_step_count += 1
        if self._train_step_count % self.target_update_freq == 0:
            self.update_target_networks()

        return loss.item()

    def update_target_networks(self):
        """Hard update: copy online network weights into target networks."""
        self.target_q_network.load_state_dict(self.q_network.state_dict())
        self.target_mixing_network.load_state_dict(self.mixing_network.state_dict())


# -----------------------------------------------------------------------
# Quick manual sanity check (run this file directly to test it works).
# Simulates a few random environment steps + training updates, with no
# real environment involved yet -- just checks the wiring is correct.
# -----------------------------------------------------------------------
if __name__ == "__main__":
    import numpy as np

    n_agents = 14
    obs_dim = 12
    state_dim = n_agents * obs_dim

    trainer = QMIXTrainer(n_agents=n_agents, obs_dim=obs_dim, state_dim=state_dim)

    # Fill the buffer with fake random experience.
    for _ in range(200):
        obs = np.random.rand(n_agents, obs_dim)
        actions = trainer.select_actions_for_all_agents(obs, epsilon=1.0)  # fully random
        reward = np.random.rand()
        next_obs = np.random.rand(n_agents, obs_dim)
        state = obs.flatten()
        next_state = next_obs.flatten()
        done = False

        trainer.replay_buffer.push(obs, actions, reward, next_obs, state, next_state, done)

    print("Buffer size:", len(trainer.replay_buffer))

    # Run a few training steps and print the loss to confirm it's computing.
    for step in range(5):
        loss = trainer.train_step(batch_size=32)
        print(f"train_step {step}: loss = {loss}")