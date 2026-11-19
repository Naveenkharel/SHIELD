import streamlit as st
import pandas as pd

from components.metrics_display import (
    display_live_metrics, display_router_metrics, display_anomaly_gauge
)
from components.comparison_charts import (
    create_comparison_charts, create_action_distribution_chart
)
from components.shap_display import (
    display_shap_explanation, display_shap_summary
)
from components.controls import display_controls

from utils.data_simulator import NetworkDataSimulator
from utils.shap_explainer import MockSHAPExplainer


st.set_page_config(page_title="MARL DDoS Defense Dashboard", page_icon="🛡️", layout="wide")


def load_css():
    try:
        with open('assets/style.css') as f:
            st.markdown(f'<style>{f.read()}</style>', unsafe_allow_html=True)
    except FileNotFoundError:
        st.markdown("""
            <style>
            .main-header { font-size: 2.5rem; font-weight: 700; color: #1a237e; padding: 1rem 0rem; border-bottom: 3px solid #0d47a1; }
            .metric-card { background: white; padding: 1rem; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); text-align: center; }
            .metric-value { font-size: 1.8rem; font-weight: 700; color: #1a237e; }
            .metric-label { font-size: 0.8rem; color: #757575; text-transform: uppercase; }
            </style>
        """, unsafe_allow_html=True)

load_css()


def init_session_state():
    if 'simulator' not in st.session_state:
        st.session_state.simulator = NetworkDataSimulator(num_routers=14)
    if 'data_history' not in st.session_state:
        st.session_state.data_history = {'protected': [], 'unprotected': []}
    if 'shap_data' not in st.session_state:
        st.session_state.shap_data = {}
    if 'attack_launched' not in st.session_state:
        st.session_state.attack_launched = False
    if 'metrics_history' not in st.session_state:
        st.session_state.metrics_history = []

init_session_state()


st.markdown('<div class="main-header">🛡️ Decentralized MARL for Adaptive DDoS Defense</div>', unsafe_allow_html=True)
st.caption("Network Behavioral Pattern Analysis for DDoS Mitigation | HCE CT-755")


with st.sidebar:
    st.markdown("### 📋 Project Info")
    st.caption("Decentralized Multi-Agent RL\nAdaptive DDoS Defense System\nNSFNET Topology (14 Routers)\nQMIX Algorithm\nSHAP Explainability")
    st.markdown("---")
    control_settings = display_controls(st.session_state.simulator)
    st.markdown("---")
    st.markdown("### 📊 System Status")
    if st.session_state.simulator.attack_active:
        st.warning(f"⚠️ Attack Active: {st.session_state.simulator.attack_type}")
    else:
        st.success("✅ System Nominal")
    st.markdown("---")
    st.markdown("### 👥 Team Members")
    st.caption("Keshav Raj Chaudhary (HCE079BCT011)\nNaveen Sharma Kharal (HCE079BCT020)\nNischhal Rijal (HCE079BCT021)\nSiddhartha Bhattarai (HCE079BCT039)")


tab1, tab2, tab3, tab4 = st.tabs(["📊 Live Dashboard", "📈 Network Comparison", "🤖 MARL Decisions & SHAP", "📋 System Logs"])


with tab1:
    col1, col2, col3 = st.columns([3, 1, 1])
    with col2:
        auto_refresh = st.checkbox("Auto Refresh", value=True)
    with col3:
        refresh_rate = st.slider("Refresh (sec)", 0.5, 5.0, 1.0, step=0.5)
    
    if auto_refresh or st.button("🔄 Refresh Now"):
        with st.spinner("Generating network metrics..."):
            metrics = st.session_state.simulator.generate_metrics()
            st.session_state.metrics_history.append(metrics)
            if len(st.session_state.metrics_history) > 100:
                st.session_state.metrics_history.pop(0)
            
            protected_data = {
                'legit_throughput': metrics['global_stats']['total_legit_throughput'],
                'attack_traffic': metrics['global_stats']['total_attack_traffic'],
                'queue_length': metrics['global_stats']['avg_queue_length'],
                'anomaly_score': metrics['global_stats']['avg_anomaly_score']
            }
            st.session_state.data_history['protected'].append(protected_data)
            
            shap_explainer = MockSHAPExplainer()
            st.session_state.shap_data = shap_explainer.generate_batch_explanations(metrics['routers'])
    
    if st.session_state.metrics_history:
        current_metrics = st.session_state.metrics_history[-1]
        display_live_metrics(current_metrics)
        
        col1, col2 = st.columns([2, 1])
        with col1:
            st.markdown("### 📊 Router Metrics")
            display_router_metrics(current_metrics.get('routers',{}))
            