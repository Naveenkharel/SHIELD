"""
agents/q_network.py

Individual per-agent Q-network for QMIX.

This is a plain feedforward network (MLP) shared across all 14 router
agents. It takes a router's local observation (12 features) and outputs
a Q-value for each of the 5 discrete mitigation actions.

Architecture:
    Input (obs_dim=12) -> Linear(64) -> ReLU -> Linear(64) -> ReLU -> Linear(n_actions=5)

Note: weights are SHARED across all agents (see project decision log).
Only the observation fed in differs per router.
"""

import torch
import torch.nn as nn


class QNetwork(nn.Module):
    """
    A simple 2-hidden-layer MLP that maps a router's local observation
    to Q-values for each possible mitigation action.
    """

    def __init__(self, obs_dim: int = 12, n_actions: int = 5, hidden_dim: int = 64):
        """
        Args:
            obs_dim: size of the observation vector (default 12, see
                     Section 4.9.1: queue length, queue growth rate,
                     arrival rate, entropy, concentration, anomaly score,
                     + 3 neighbors x (queue, arrival rate)).
            n_actions: number of discrete actions (default 5: Allow,
                       Rate-limit, Throttle, Drop, Reroute).
            hidden_dim: number of units in each hidden layer.
        """
        super().__init__()

        self.obs_dim = obs_dim
        self.n_actions = n_actions

        self.fc1 = nn.Linear(obs_dim, hidden_dim)
        self.fc2 = nn.Linear(hidden_dim, hidden_dim)
        self.fc3 = nn.Linear(hidden_dim, n_actions)

        self.relu = nn.ReLU()

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        """
        Forward pass.

        Args:
            obs: tensor of shape (batch_size, obs_dim) or (obs_dim,)
                 containing a router's local observation.

        Returns:
            q_values: tensor of shape (batch_size, n_actions) or
                      (n_actions,) with one Q-value per action.
        """
        x = self.relu(self.fc1(obs))
        x = self.relu(self.fc2(x))
        q_values = self.fc3(x)  # no activation on output — raw Q-values
        return q_values


# -----------------------------------------------------------------------
# Quick manual sanity check (run this file directly to test it works).
# This is NOT part of the training pipeline — just a smoke test so you
# can confirm the shapes are correct before wiring it into the agent.
#
#  -----------------------------------------------------------------------
""" for testing, run this file directly: `python agents/q_network.py` 
if __name__ == "__main__":
    obs_dim = 12
    n_actions = 5

    net = QNetwork(obs_dim=obs_dim, n_actions=n_actions)

    # Single observation (simulating one router at one timestep)
    dummy_obs = torch.rand(obs_dim)
    q_vals = net(dummy_obs)
    print("Single obs -> Q-values:", q_vals.shape, q_vals)

    # Batch of observations (simulating a training batch)
    batch_size = 8
    dummy_batch = torch.rand(batch_size, obs_dim)
    q_vals_batch = net(dummy_batch)
    print("Batch obs -> Q-values:", q_vals_batch.shape)"""