import streamlit as st
import plotly.graph_objects as go
import plotly.express as px
import pandas as pd
from typing import Dict

def display_live_metrics(metrics_data: Dict):
    """Display live network metrics in dashboard"""
    
    if not metrics_data:
        st.warning("Waiting for data...")
        return
    
    col1, col2, col3, col4 = st.columns(4)
    
    # Get global stats
    stats = metrics_data.get('global_stats', {})
    
    with col1:
        st.metric(
            label="Total Legitimate Throughput",
            value=f"{stats.get('total_legit_throughput', 0):.0f} Mbps",
            delta="Normal" if metrics_data.get('attack_active', False) else "Stable"
        )
    
    with col2:
        st.metric(
            label="Attack Traffic",
            value=f"{stats.get('total_attack_traffic', 0):.0f} Mbps",
            delta=f"{metrics_data.get('attack_intensity', 0)}%" if metrics_data.get('attack_active', False) else "None"
        )
    
    with col3:
        st.metric(
            label="Avg Queue Length",
            value=f"{stats.get('avg_queue_length', 0):.1f} packets",
            delta="Growing" if metrics_data.get('attack_active', False) else "Stable"
        )
    
    with col4:
        st.metric(
            label="Avg Anomaly Score",
            value=f"{stats.get('avg_anomaly_score', 0):.1f}",
            delta="High" if stats.get('avg_anomaly_score', 0) > 50 else "Normal"
        )

def display_router_metrics(router_data: Dict):
    """Display metrics for individual routers"""
    
    if not router_data:
        st.warning("No router data available")
        return
    
    # Create a DataFrame for router metrics
    df_data = []
    for router, metrics in router_data.items():
        df_data.append({
            'Router': router,
            'Queue Length': metrics.get('queue_length', 0),
            'Queue Growth': metrics.get('queue_growth_rate', 0),
            'Arrival Rate': metrics.get('packet_arrival_rate', 0),
            'Anomaly Score': metrics.get('anomaly_score', 0),
            'Action': metrics.get('action', 'Unknown')
        })
    
    df = pd.DataFrame(df_data)
    
    # Color coding for actions
    action_colors = {
        'Allow': '#2ecc71',
        'Rate-limit': '#f1c40f',
        'Throttle': '#e67e22',
        'Drop': '#e74c3c',
        'Reroute': '#3498db'
    }
    df['color'] = df['Action'].map(action_colors).fillna('#95a5a6')
    
    # Display as a table with styling
    st.dataframe(
        df.style.apply(lambda x: ['background-color: {}'.format(c) for c in x['color']], axis=1),
        use_container_width=True
    )
    
    return df

def display_anomaly_gauge(anomaly_score: float):
    """Display anomaly score as a gauge chart"""
    
    fig = go.Figure(go.Indicator(
        mode="gauge+number+delta",
        value=anomaly_score,
        domain={'x': [0, 1], 'y': [0, 1]},
        title={'text': "Network Anomaly Score"},
        delta={'reference': 50},
        gauge={
            'axis': {'range': [0, 100]},
            'bar': {'color': "darkblue"},
            'steps': [
                {'range': [0, 30], 'color': "lightgreen"},
                {'range': [30, 70], 'color': "lightyellow"},
                {'range': [70, 100], 'color': "lightcoral"}
            ],
            'threshold': {
                'line': {'color': "red", 'width': 4},
                'thickness': 0.75,
                'value': 90
            }
        }
    ))
    
    fig.update_layout(height=200)
    st.plotly_chart(fig, use_container_width=True)