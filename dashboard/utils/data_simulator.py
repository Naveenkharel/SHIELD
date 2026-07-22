
import numpy as np
import random
from datetime import datetime
from typing import Dict, List, Any, Optional


class NetworkDataSimulator:
    """
    Simulates network telemetry data for dashboard development.
    
    Attributes:
        num_routers (int): Number of routers in the network (default: 14)
        num_neighbors (int): Number of neighbors per router (default: 3)
        attack_active (bool): Whether an attack is currently active
        attack_type (str): Type of attack (UDP/TCP/ICMP)
        attack_intensity (int): Attack intensity percentage (10-100)
        routers (List[str]): List of router IDs
        time_step (int): Current simulation time step
    """
    
    def __init__(self, num_routers: int = 14, num_neighbors: int = 3):
        """
        Initialize the network data simulator.
        
        Args:
            num_routers: Number of routers to simulate
            num_neighbors: Number of neighbors per router
        """
        self.num_routers = num_routers
        self.num_neighbors = num_neighbors
        self.attack_active = False
        self.attack_type: Optional[str] = None
        self.attack_intensity = 50
        self.time_step = 0
        self.routers = [f"R{i+1}" for i in range(num_routers)]
        
        # Constants for normal traffic
        self.BASELINE_THROUGHPUT = 950  # Mbps
        self.BASELINE_ARRIVAL = 5000     # packets/sec
        self.BASELINE_QUEUE = 10         # packets
        
    def generate_metrics(self) -> Dict[str, Any]:
        """
        Generate realistic network metrics for all routers.
        
        Returns:
            Dict containing:
                - timestamp: ISO format timestamp
                - routers: Per-router metrics dictionary
                - attack_active: Boolean attack status
                - attack_type: Attack type string
                - attack_intensity: Intensity percentage
                - global_stats: Aggregated network statistics
        """
        self.time_step += 1
        
        # Generate base legitimate traffic
        base_legit = self.BASELINE_THROUGHPUT * (0.85 + np.random.normal(0, 0.05))
        
        # Calculate attack impact if active
        if self.attack_active:
            attack_factor = self.attack_intensity / 100.0
            legit_impact = np.random.uniform(0.2, 0.85) * attack_factor
            legit_throughput = base_legit * (1 - legit_impact * 0.75)
            attack_traffic = np.random.uniform(50, 700) * attack_factor
            queue_overflow = np.random.uniform(0.15, 0.85) * attack_factor * 1.5
        else:
            legit_throughput = base_legit * (0.9 + np.random.normal(0, 0.03))
            attack_traffic = np.random.uniform(0, 30)
            queue_overflow = np.random.uniform(0, 0.08)
        
        # Generate per-router data
        router_data = {}
        for router in self.routers:
            # Router-specific variation (edge vs backbone)
            router_factor = np.random.uniform(0.7, 1.3)
            
            # Queue metrics
            queue_length = max(0, self.BASELINE_QUEUE + 
                              50 * queue_overflow * router_factor + 
                              np.random.normal(0, 3))
            queue_growth = max(0, queue_length - np.random.uniform(0, 15))
            
            # Packet arrival rate
            base_arrival = self.BASELINE_ARRIVAL + np.random.normal(0, 400)
            if self.attack_active:
                arrival_rate = base_arrival + np.random.uniform(500, 4500) * attack_factor
            else:
                arrival_rate = base_arrival * (0.85 + np.random.normal(0, 0.08))
            
            # Flow entropy (higher during attacks due to more flows)
            entropy = np.random.uniform(4.5, 7.5)
            if self.attack_active:
                entropy += np.random.uniform(0.5, 2.5) * attack_factor
            
            # Flow concentration (higher during attacks)
            concentration = np.random.uniform(0.05, 0.25)
            if self.attack_active:
                concentration += np.random.uniform(0.05, 0.35) * attack_factor
            
            # Composite anomaly score
            anomaly = self._compute_anomaly_score(
                queue_length, queue_growth, arrival_rate,
                entropy, concentration
            )
            
            # MARL agent action
            action = self._simulate_marl_action(anomaly)
            
            router_data[router] = {
                'legit_throughput': legit_throughput * router_factor,
                'attack_traffic': attack_traffic * 0.5 * router_factor,
                'queue_length': round(max(0, queue_length), 2),
                'queue_growth_rate': round(queue_growth, 2),
                'packet_arrival_rate': round(arrival_rate, 0),
                'flow_entropy': round(entropy, 3),
                'flow_concentration': round(concentration, 3),
                'anomaly_score': round(anomaly, 2),
                'action': action
            }
        
        return {
            'timestamp': datetime.now().isoformat(),
            'routers': router_data,
            'attack_active': self.attack_active,
            'attack_type': self.attack_type,
            'attack_intensity': self.attack_intensity,
            'global_stats': self._compute_global_stats(router_data)
        }
    
    def _compute_anomaly_score(self, queue: float, queue_growth: float,
                               arrival: float, entropy: float,
                               concentration: float) -> float:
        """
        Compute composite anomaly score using weighted Z-scores.
        
        The anomaly score ranges from 0-100 and is computed as:
        - Queue length deviation (25% weight)
        - Queue growth rate (20% weight)
        - Packet arrival rate (25% weight)
        - Flow entropy (15% weight)
        - Flow concentration (15% weight)
        
        Returns:
            float: Anomaly score between 0 and 100
        """
        # Z-score approximations
        z_queue = max(0, (queue - self.BASELINE_QUEUE) / 15)
        z_growth = min(4, queue_growth / 8)
        z_arrival = max(0, (arrival - self.BASELINE_ARRIVAL) / 1500)
        z_entropy = min(4, (entropy - 4.5) / 1.2)
        z_concentration = min(4, (concentration - 0.05) / 0.12)
        
        # Weighted sum
        anomaly = (
            0.25 * z_queue +
            0.20 * z_growth +
            0.25 * z_arrival +
            0.15 * z_entropy +
            0.15 * z_concentration
        ) * 20
        
        # Boost during attacks
        if self.attack_active:
            anomaly += np.random.uniform(5, 25) * (self.attack_intensity / 100.0)
        
        return min(100, max(0, anomaly))
    
    def _simulate_marl_action(self, anomaly: float) -> str:
        """
        Simulate MARL agent action based on anomaly score.
        
        Action thresholds:
            - 0-20: Allow (normal traffic)
            - 20-40: Rate-limit or Allow
            - 40-60: Rate-limit or Throttle
            - 60-80: Throttle or Drop
            - 80-100: Throttle, Drop, or Reroute
        
        Returns:
            str: Action name
        """
        if anomaly < 20:
            return "Allow"
        elif anomaly < 40:
            return "Rate-limit" if random.random() > 0.5 else "Allow"
        elif anomaly < 60:
            return random.choice(["Rate-limit", "Throttle"])
        elif anomaly < 80:
            return random.choice(["Throttle", "Drop"])
        else:
            return random.choice(["Throttle", "Drop", "Reroute"])
    
    def _compute_global_stats(self, router_data: Dict) -> Dict[str, Any]:
        """
        Compute aggregated network statistics.
        
        Args:
            router_data: Dictionary of per-router metrics
            
        Returns:
            Dict with aggregated statistics
        """
        total_legit = sum(d['legit_throughput'] for d in router_data.values())
        total_attack = sum(d['attack_traffic'] for d in router_data.values())
        avg_queue = np.mean([d['queue_length'] for d in router_data.values()])
        avg_anomaly = np.mean([d['anomaly_score'] for d in router_data.values()])
        
        # Action distribution
        action_dist = {}
        for d in router_data.values():
            action = d['action']
            action_dist[action] = action_dist.get(action, 0) + 1
        
        return {
            'total_legit_throughput': round(total_legit, 2),
            'total_attack_traffic': round(total_attack, 2),
            'avg_queue_length': round(avg_queue, 2),
            'avg_anomaly_score': round(avg_anomaly, 2),
            'action_distribution': action_dist
        }
    
    def toggle_attack(self, attack_type: str = 'UDP Flood', 
                      intensity: int = 50) -> bool:
        """
        Toggle attack simulation on/off.
        
        Args:
            attack_type: Type of attack ('UDP Flood', 'TCP SYN Flood', 'ICMP Flood')
            intensity: Attack intensity percentage (10-100)
            
        Returns:
            bool: New attack state
        """
        self.attack_active = not self.attack_active
        self.attack_type = attack_type if self.attack_active else None
        self.attack_intensity = intensity if self.attack_active else 0
        return self.attack_active
    
    def reset(self) -> None:
        """Reset simulation to clean state."""
        self.attack_active = False
        self.attack_type = None
        self.attack_intensity = 50
        self.time_step = 0