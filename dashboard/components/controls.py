import streamlit as st
from datetime import datetime

def display_controls(simulator):
    """Display attack controls and settings"""
    
    st.subheader("🎮 Attack Controls")
    
    col1, col2, col3 = st.columns([2, 2, 1])
    
    with col1:
        attack_type = st.selectbox(
            "Attack Type",
            ["UDP Flood", "TCP SYN Flood", "ICMP Flood"]
        )
    
    with col2:
        attack_intensity = st.slider(
            "Attack Intensity",
            min_value=10,
            max_value=100,
            value=50,
            step=5,
            help="Percentage of link capacity"
        )
    
    with col3:
        if st.button("🚀 Launch Attack", type="primary"):
            simulator.toggle_attack(attack_type, attack_intensity)
            st.session_state.attack_launched = True
            st.success(f"Attack launched: {attack_type} at {attack_intensity}%")
    
    # Reset button
    if st.button("🔄 Reset Simulation", type="secondary"):
        simulator.reset()
        st.session_state.attack_launched = False
        st.session_state.data_history = {}
        st.success("Simulation reset to clean state")
    
    # Defense mode selector
    defense_mode = st.selectbox(
        "Defense Mode",
        ["Label-Free MARL", "Static Rate-Limiting", "Centralized RL", "No Defense"],
        index=0,
        help="Select the defense mechanism to evaluate"
    )
    
    # Display current status
    status_col1, status_col2 = st.columns(2)
    
    with status_col1:
        if st.session_state.get('attack_launched', False):
            st.info(f"🔴 Attack Active: {attack_type} ({attack_intensity}%)")
        else:
            st.success("🟢 No Attack Active")
    
    with status_col2:
        st.info(f"🛡️ Defense: {defense_mode}")
    
    return {
        'attack_type': attack_type,
        'attack_intensity': attack_intensity,
        'defense_mode': defense_mode
    }