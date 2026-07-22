import streamlit as st
import plotly.graph_objects as go
import plotly.express as px
import pandas as pd
from typing import Dict, List, Optional


def create_comparison_charts(data_history: Dict) -> None:
    if not data_history:
        st.warning("📊 No historical data available. Run simulation to see charts.")
        return
    
    protected_data = data_history.get('protected', [])
    unprotected_data = data_history.get('unprotected', [])
    
    col1, col2 = st.columns(2)
    
    with col1:
        st.markdown("### 🛡️ Protected Network")
        st.caption("MARL-based adaptive defense")
        if protected_data:
            fig = _create_throughput_chart(protected_data, "Protected")
            st.plotly_chart(fig, use_container_width=True)
        else:
            st.info("No protected network data available")
    
    with col2:
        st.markdown("### 💀 Unprotected Network")
        st.caption("No defense mechanism")
        if unprotected_data:
            fig = _create_throughput_chart(unprotected_data, "Unprotected")
            st.plotly_chart(fig, use_container_width=True)
        else:
            st.info("No unprotected network data available")


def _create_throughput_chart(data: List[Dict], title: str) -> go.Figure:
    if not data:
        return go.Figure()
    
    time_points = list(range(len(data)))
    
    fig = go.Figure()
    
    legit_values = [d.get('legit_throughput', 0) for d in data]
    fig.add_trace(go.Scatter(
        x=time_points,
        y=legit_values,
        name='Legitimate Traffic',
        line=dict(color='#2e7d32', width=2.5),
        fill='tozeroy',
        fillcolor='rgba(46, 125, 50, 0.1)'
    ))
    
    attack_values = [d.get('attack_traffic', 0) for d in data]
    fig.add_trace(go.Scatter(
        x=time_points,
        y=attack_values,
        name='Attack Traffic',
        line=dict(color='#c62828', width=2, dash='dash')
    ))
    
    queue_values = [d.get('queue_length', 0) * 10 for d in data]
    fig.add_trace(go.Scatter(
        x=time_points,
        y=queue_values,
        name='Queue Length (x10)',
        line=dict(color='#e65100', width=1.5, dash='dot'),
        yaxis='y2'
    ))
    
    threshold_values = [40] * len(data)
    fig.add_trace(go.Scatter(
        x=time_points,
        y=threshold_values,
        name='Alert Threshold',
        line=dict(color='rgba(198, 40, 40, 0.5)', width=1, dash='dash'),
        opacity=0.6
    ))
    
    fig.update_layout(
        title=dict(
            text=f"{title} Network Performance",
            font=dict(size=14)
        ),
        xaxis=dict(
            title='Time Step (100ms)',
            showgrid=True,
            gridcolor='rgba(0,0,0,0.05)'
        ),
        yaxis=dict(
            title='Throughput (Mbps)',
            showgrid=True,
            gridcolor='rgba(0,0,0,0.05)'
        ),
        yaxis2=dict(
            title='Queue Length (scaled)',
            overlaying='y',
            side='right',
            showgrid=False
        ),
        hovermode='x unified',
        height=350,
        legend=dict(
            orientation='h',
            yanchor='bottom',
            y=1.02,
            xanchor='right',
            x=1
        ),
        margin=dict(l=40, r=40, t=60, b=40)
    )
    
    return fig


def create_action_distribution_chart(action_distribution: Dict) -> None:
    if not action_distribution:
        st.info("No actions taken yet")
        return
    
    colors = {
        'Allow': '#2e7d32',
        'Rate-limit': '#f57f17',
        'Throttle': '#e65100',
        'Drop': '#c62828',
        'Reroute': '#0d47a1'
    }
    
    color_list = [colors.get(action, '#616161') for action in action_distribution.keys()]
    
    fig = go.Figure(data=[go.Pie(
        labels=list(action_distribution.keys()),
        values=list(action_distribution.values()),
        hole=0.4,
        marker=dict(colors=color_list),
        textinfo='label+percent',
        textposition='auto',
        hoverinfo='label+value+percent'
    )])
    
    fig.update_layout(
        title=dict(
            text="MARL Agent Action Distribution",
            font=dict(size=14)
        ),
        height=350,
        margin=dict(l=20, r=20, t=60, b=20)
    )
    
    st.plotly_chart(fig, use_container_width=True)