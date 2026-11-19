import numpy as np
import pandas as pd
from typing import Dict, List, Tuple, Optional
from datetime import datetime


class MockSHAPExplainer:
    
    def __init__(self):
        self.feature_names = [
            'queue_length',
            'queue_growth_rate',
            'packet_arrival_rate',
            'flow_entropy',
            'flow_concentration',
            'anomaly_score'
        ]
        
        self.action_feature_weights = {
            'Allow': {
                'queue_length': 0.10,
                'queue_growth_rate': 0.05,
                'packet_arrival_rate': 0.15,
                'flow_entropy': 0.35,
                'flow_concentration': 0.25,
                'anomaly_score': 0.10
            },
            'Rate-limit': {
                'queue_length': 0.25,
                'queue_growth_rate': 0.20,
                'packet_arrival_rate': 0.25,
                'flow_entropy': 0.10,
                'flow_concentration': 0.10,
                'anomaly_score': 0.10
            },
            'Throttle': {
                'queue_length': 0.20,
                'queue_growth_rate': 0.25,
                'packet_arrival_rate': 0.20,
                'flow_entropy': 0.10,
                'flow_concentration': 0.10,
                'anomaly_score': 0.15
            },
            'Drop': {
                'queue_length': 0.20,
                'queue_growth_rate': 0.30,
                'packet_arrival_rate': 0.20,
                'flow_entropy': 0.10,
                'flow_concentration': 0.10,
                'anomaly_score': 0.10
            },
            'Reroute': {
                'queue_length': 0.15,
                'queue_growth_rate': 0.10,
                'packet_arrival_rate': 0.15,
                'flow_entropy': 0.30,
                'flow_concentration': 0.20,
                'anomaly_score': 0.10
            }
        }
        
        self.feature_ranges = {
            'queue_length': (0, 100),
            'queue_growth_rate': (0, 50),
            'packet_arrival_rate': (0, 15000),
            'flow_entropy': (0, 10),
            'flow_concentration': (0, 1),
            'anomaly_score': (0, 100)
        }
    
    def generate_explanation(self, router_id: str, action: str,
                            metrics: Dict) -> Dict:
        base_weights = self.action_feature_weights.get(
            action, self.action_feature_weights['Allow']
        )
        
        explanations = {}
        for feature, base_weight in base_weights.items():
            variation = np.random.normal(0, 0.08)
            weight = max(0.01, min(1.0, base_weight + variation))
            
            if feature in metrics and feature in self.feature_ranges:
                normalized_value = self._normalize_metric(feature, metrics[feature])
                weight = weight * (0.4 + 0.6 * normalized_value)
            
            explanations[feature] = weight
        
        total = sum(explanations.values())
        if total > 0:
            explanations = {k: v/total for k, v in explanations.items()}
        
        is_attack = metrics.get('anomaly_score', 0) > 30
        
        return {
            'router_id': router_id,
            'action': action,
            'feature_importance': explanations,
            'timestamp': datetime.now().isoformat(),
            'is_attack_decision': is_attack,
            'top_features': self._get_top_features(explanations, 3)
        }
    
    def _normalize_metric(self, feature: str, value: float) -> float:
        min_val, max_val = self.feature_ranges.get(feature, (0, 100))
        if max_val == min_val:
            return 0.5
        return max(0, min(1, (value - min_val) / (max_val - min_val)))
    
    def _get_top_features(self, explanations: Dict, 
                          n: int = 3) -> List[Tuple[str, float]]:
        sorted_features = sorted(
            explanations.items(),
            key=lambda x: x[1],
            reverse=True
        )
        return sorted_features[:n]
    
    def generate_batch_explanations(self, router_data: Dict) -> Dict:
        explanations = {}
        for router_id, metrics in router_data.items():
            if 'action' in metrics:
                explanations[router_id] = self.generate_explanation(
                    router_id,
                    metrics['action'],
                    metrics
                )
        return explanations