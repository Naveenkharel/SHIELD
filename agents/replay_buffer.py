"""
agents/replay_buffer.py

Replay Buffer for QMIX training.

Stores one experience per timestep, where each experience captures the
WHOLE TEAM's situation (all 14 agents at once) -- not just a single
agent -- because QMIX trains on joint experience.

Each stored experience is a tuple of:
    obs           : (n_agents, obs_dim)   -- all agents' local observations
    actions       : (n_agents,)           -- action index each agent took
    reward        : float                 -- shared team reward
    next_obs      : (n_agents, obs_dim)   -- all agents' obs at t+1
    state         : (state_dim,)          -- global state at t (concatenated obs)
    next_state    : (state_dim,)          -- global state at t+1
    done          : bool                  -- whether episode ended at this step

Sampling returns a random batch of these, stacked into tensors, ready to
be fed into QNetwork and MixingNetwork (agents/q_network.py,
agents/mixing_network.py).
"""

import random
from collections import deque, namedtuple

import numpy as np
import torch

# A single timestep of team experience.
Experience = namedtuple(
    "Experience",
    ["obs", "actions", "reward", "next_obs", "state", "next_state", "done"],
)


class ReplayBuffer:
    """
    Fixed-capacity ring buffer storing team-level experiences for QMIX.
    Oldest experiences are automatically discarded once capacity is reached.
    """

    def __init__(self, capacity: int = 50000):
        """
        Args:
            capacity: maximum number of experiences to store before old
                      ones start getting overwritten.
        """
        self.capacity = capacity
        self.buffer = deque(maxlen=capacity)

    def push(self, obs, actions, reward, next_obs, state, next_state, done):
        """
        Store one timestep of team experience.

        Args:
            obs: array-like, shape (n_agents, obs_dim)
            actions: array-like, shape (n_agents,) -- ints, action index per agent
            reward: float -- shared team reward for this timestep
            next_obs: array-like, shape (n_agents, obs_dim)
            state: array-like, shape (state_dim,) -- global state at t
            next_state: array-like, shape (state_dim,) -- global state at t+1
            done: bool -- True if episode ended at this timestep
        """
        experience = Experience(
            obs=np.asarray(obs, dtype=np.float32),
            actions=np.asarray(actions, dtype=np.int64),
            reward=float(reward),
            next_obs=np.asarray(next_obs, dtype=np.float32),
            state=np.asarray(state, dtype=np.float32),
            next_state=np.asarray(next_state, dtype=np.float32),
            done=bool(done),
        )
        self.buffer.append(experience)

    def sample(self, batch_size: int):
        """
        Randomly sample a batch of experiences and stack them into tensors
        ready for training.

        Args:
            batch_size: number of experiences to sample.

        Returns:
            dict of tensors:
                obs        : (batch_size, n_agents, obs_dim)
                actions    : (batch_size, n_agents)
                reward     : (batch_size, 1)
                next_obs   : (batch_size, n_agents, obs_dim)
                state      : (batch_size, state_dim)
                next_state : (batch_size, state_dim)
                done       : (batch_size, 1)
        """
        batch = random.sample(self.buffer, batch_size)

        obs_batch = torch.tensor(np.stack([e.obs for e in batch]))
        actions_batch = torch.tensor(np.stack([e.actions for e in batch]))
        reward_batch = torch.tensor(
            np.array([e.reward for e in batch], dtype=np.float32)
        ).view(-1, 1)
        next_obs_batch = torch.tensor(np.stack([e.next_obs for e in batch]))
        state_batch = torch.tensor(np.stack([e.state for e in batch]))
        next_state_batch = torch.tensor(np.stack([e.next_state for e in batch]))
        done_batch = torch.tensor(
            np.array([e.done for e in batch], dtype=np.float32)
        ).view(-1, 1)

        return {
            "obs": obs_batch,
            "actions": actions_batch,
            "reward": reward_batch,
            "next_obs": next_obs_batch,
            "state": state_batch,
            "next_state": next_state_batch,
            "done": done_batch,
        }

    def __len__(self):
        return len(self.buffer)

    def is_ready(self, batch_size: int) -> bool:
        """Convenience check: True once buffer has enough experiences to sample a batch."""
        return len(self.buffer) >= batch_size


# -----------------------------------------------------------------------
# Quick manual sanity check (run this file directly to test it works).
# -----------------------------------------------------------------------
if __name__ == "__main__":
    n_agents = 14
    obs_dim = 12
    state_dim = n_agents * obs_dim  # 168

    buffer = ReplayBuffer(capacity=1000)

    # Simulate pushing 100 fake timesteps of team experience.
    for _ in range(100):
        fake_obs = np.random.rand(n_agents, obs_dim)
        fake_actions = np.random.randint(0, 5, size=n_agents)
        fake_reward = np.random.rand()
        fake_next_obs = np.random.rand(n_agents, obs_dim)
        fake_state = fake_obs.flatten()
        fake_next_state = fake_next_obs.flatten()
        fake_done = False

        buffer.push(fake_obs, fake_actions, fake_reward, fake_next_obs,
                    fake_state, fake_next_state, fake_done)

    print("Buffer size:", len(buffer))
    print("Ready for batch of 32:", buffer.is_ready(32))

    batch = buffer.sample(batch_size=32)
    for key, val in batch.items():
        print(f"{key}: {val.shape}")