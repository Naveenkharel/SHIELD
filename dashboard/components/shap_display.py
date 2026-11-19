import streamlit as st
import plotly.graph_objects as go
import pandas as pd
import numpy as np
from typing import Dict, List

def display_shap_explanation(shap_data: Dict, router_id: str = None):
    """Display SHAP explanation as a bar chart"""
    
    if not shap_data:
        st.info("Waiting for SHAP explanations...")
        return
    
    # Filter for specific router if requested
    if router_id and router_id in shap_data:
        explanation = shap_data[router_id]
        _create_shap_bar_chart(explanation, router_id)
    else:
        # Display all routers in expandable sections
        with st.expander("View All Router Explanations", expanded=False):
            for router, explanation in shap_data.items():
                _create_shap_bar_chart(explanation, router)
                st.divider()

def _create_shap_bar_chart(explanation: Dict, router_id: str):
    """Create a SHAP bar chart for a single router"""
    
    feature_importance = explanation.get('feature_importance', {})
    action = explanation.get('action', 'Unknown')
    is_attack = explanation.get('is_attack_decision', False)
    top_features = explanation.get('top_features', [])
    
    if not feature_importance:
        st.info(f"No explanation available for {router_id}")
        return
    
    # Sort features by importance
    sorted_features = sorted(
        feature_importance.items(),
        key=lambda x: x[1],
        reverse=True
    )
    
    # Create DataFrame for plotting
    df = pd.DataFrame(sorted_features, columns=['Feature', 'SHAP Value'])
    
    # Color coding based on impact
    colors = ['#e74c3c' if v > 0.2 else '#f39c12' if v > 0.1 else '#3498db' 
              for v in df['SHAP Value']]
    
    # Create horizontal bar chart
    fig = go.Figure()
    
    fig.add_trace(go.Bar(
        x=df['SHAP Value'],
        y=df['Feature'],
        orientation='h',
        marker_color=colors,
        text=[f"{v:.1%}" for v in df['SHAP Value']],
        textposition='outside'
    ))
    
    # Create title
    attack_indicator = "🔴 Attack Detection" if is_attack else "🟢 Normal Traffic"
    title = f"Router {router_id} - {action} Action ({attack_indicator})"
    
    fig.update_layout(
        title=title,
        xaxis_title='Feature Importance',
        yaxis_title='Feature',
        height=300,
        showlegend=False,
        margin=dict(l=0, r=0, t=40, b=0)
    )
    
    st.plotly_chart(fig, use_container_width=True)
    
    # Show top features text
    if top_features:
        top_text = ", ".join([f"{f}: {v:.1%}" for f, v in top_features])
        st.caption(f"Top features: {top_text}")

def display_shap_summary(shap_data: Dict):
    """Display summary of all SHAP explanations"""
    
    if not shap_data:
        return
    
    # Count actions per router
    action_counts = {}
    router_actions = {}
    
    for router, explanation in shap_data.items():
        action = explanation.get('action', 'Unknown')
        action_counts[action] = action_counts.get(action, 0) + 1
        router_actions[router] = action
    
    col1, col2 = st.columns(2)
    
    with col1:
        st.write("**Decision Summary**")
        # Display action distribution
        df_actions = pd.DataFrame(
            list(action_counts.items()),
            columns=['Action', 'Count']
        )
        st.dataframe(df_actions, use_container_width=True)
    
    with col2:
        st.write("**Routers by Action**")
        for router, action in router_actions.items():
            st.caption(f"{router}: {action}")