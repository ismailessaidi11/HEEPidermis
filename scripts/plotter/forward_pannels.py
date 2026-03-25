import numpy as np
import matplotlib.pyplot as plt

def plot_forward_vco_point(ax, model, result):
    vin_plot = model.params.vin_range
    fosc_plot = model.fosc_from_vin(vin_plot)

    ax.scatter(
        model.vin_data, model.fosc_data,
        s=40, color="black", alpha=0.6,
        label="Measured data", zorder=4
    )
    ax.plot(vin_plot, fosc_plot, linewidth=2.5, label="VCO model")

    ax.plot(
        result.intermediate.vin_mV,
        result.intermediate.f_osc_kHz,
        'r*',
        markersize=18,
        label="Operating point",
        zorder=5
    )
    ax.axvline(result.intermediate.vin_mV, color='r', linestyle='--', alpha=0.5)

    ax.set_xlabel("V_in (mV)")
    ax.set_ylabel("f_osc (kHz)")
    ax.set_title("VCO transfer curve")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=9)

def plot_forward_summary(ax, result, model=None):
    ax.axis("off")

    txt = (
        f"G:        {result.input.G_uS:.4f} μS\n"
        f"i_dc:     {result.input.i_dc_uA:.4f} μA\n"
        f"f_s:      {result.input.fs_Hz:.4f} Hz\n\n"
        f"V_in:     {result.intermediate.vin_mV:.4f} mV\n"
        f"ΔV_in:    {result.intermediate.dVin_mV*1000:.4f} μV\n"
        f"f_osc:    {result.intermediate.f_osc_kHz:.4f} kHz\n"
        f"Δf_osc:   {result.intermediate.df_osc_Hz:.4f} Hz\n"
        f"K_VCO:    {result.intermediate.kvco_kHz_per_mV:.6f} kHz/mV\n"
    )
    
    # Add constraint if model is provided
    if model is not None and hasattr(model.params, 'v_dd') and hasattr(model.params, 'vin_range'):
        v_dd = model.params.v_dd
        v_min = model.params.vin_range[0]
        i_dc_max = result.input.G_uS * (v_dd - v_min) / 1000
        txt += f"\n─────────────\n"
        txt += f"i_dc_max: {i_dc_max:.4f} μA\nG×(V_dd-V_min)"

    ax.text(
        0.5, 0.5, txt,
        transform=ax.transAxes,
        ha='center', va='center',
        fontsize=12,
        family='monospace',
        bbox=dict(boxstyle='round', facecolor='lightblue', alpha=0.25)
    )
    ax.set_title("Inputs and operating point", fontweight='bold')

def plot_forward_df_components(ax, result):
    labels = [
        r'$\Delta f_{samp}$',
        r'$\Delta f_{adev}$',
        r'$\Delta f_{osc}$'
    ]
    values = [
        result.intermediate.df_osc_sampling_Hz,
        result.intermediate.df_osc_adev_Hz,
        result.intermediate.df_osc_Hz
    ]

    ax.bar(labels, values)
    ax.set_ylabel("Hz")
    ax.set_title("Frequency error contributions")
    ax.grid(True, axis='y', alpha=0.3)

def plot_forward_outputs(ax, result):
    # Delta_G in nS (convert from μS)
    delta_G_nS = result.output.delta_G_uS * 1000
    
    # Power metrics in μW
    power_labels = [r'$P_{iDC}$', r'$P_{VCO}$', r'$P_{CNT}$', r'$P_{TOT}$']
    power_values = [
        result.output.P_idc_uW,
        result.output.P_vco_uW,
        result.output.P_cnt_uW,
        result.output.P_tot_uW
    ]
    
    # Plot Delta_G on primary axis
    ax.bar(r'$\Delta G$', delta_G_nS, color='steelblue', alpha=0.7, width=0.4, label=r'$\Delta G$')
    ax.set_ylabel(r'$\Delta G$ (nS)', color='steelblue')
    ax.tick_params(axis='y', labelcolor='steelblue')
    
    # Create secondary axis for power metrics
    ax2 = ax.twinx()
    x_positions = np.arange(1, len(power_labels) + 1)
    ax2.bar(x_positions, power_values, color='coral', alpha=0.7, width=0.6, label='Power')
    ax2.set_xticks(np.concatenate([[0], x_positions]))
    ax2.set_xticklabels([r'$\Delta G$'] + power_labels)
    ax2.set_ylabel('Power (μW)', color='coral')
    ax2.tick_params(axis='y', labelcolor='coral')
    
    ax.set_title("Output metrics")
    ax.grid(True, axis='y', alpha=0.3)

def plot_forward_tradeoff(ax, model, result, variance=1, avg_window=1):
    G_uS = result.input.G_uS
    fs_Hz = result.input.fs_Hz
    i_vals = model.params.i_dc_range

    deltaG_vals = []
    ptot_vals = []
    p_idcs = []
    p_vco_cnt = []

    for i_dc in i_vals:
        vin_mV = model.vin_from_G(G_uS, i_dc)

        deltaG_vals.append(
            model.delta_G_uS(
                vin_mV=vin_mV,
                i_dc_uA=i_dc,
                fs_Hz=fs_Hz,
                variance=variance,
                avg_window=avg_window
            )
        )

        p_idc = model.idc_power_uW(vin_mV, i_dc)
        p_vco = model.pvco_from_vin(vin_mV)
        p_cnt = model.pcnt_from_vin(vin_mV)
        p_idcs.append(p_idc)
        p_vco_cnt.append(p_vco + p_cnt)
        ptot_vals.append(p_idc + p_vco + p_cnt)

    deltaG_vals = np.asarray(deltaG_vals, dtype=float) * 1000  # Convert to nS
    p_idcs = np.asarray(p_idcs, dtype=float)
    p_vco_cnt = np.asarray(p_vco_cnt, dtype=float)
    ptot_vals = np.asarray(ptot_vals, dtype=float)

    max_i_dc = model.i_dc_max(result.input.G_uS)
    i_vals = i_vals[i_vals <= max_i_dc] 
    i_vals = i_vals[:len(i_vals)]  # Limit to first half for better visualization
    # Plot Delta_G on primary axis (steelblue)
    ax.plot(i_vals, deltaG_vals[:len(i_vals)], linewidth=2.5, label=r'$\Delta G$', color='steelblue')
    ax.plot(result.input.i_dc_uA, result.output.delta_G_uS * 1000, 'r*', markersize=16, zorder=5)

    ax.set_xlabel(r'$i_{dc}$ (μA)')
    ax.set_ylabel(r'$\Delta G$ (nS)', color='steelblue')
    ax.tick_params(axis='y', labelcolor='steelblue')
    ax.set_title(r'Tradeoff vs $i_{dc}$')
    ax.grid(True, alpha=0.3)

    # Plot Power on secondary axis (coral)
    ax2 = ax.twinx()
    ax2.plot(i_vals, p_idcs[:len(i_vals)], linestyle=':', linewidth=2.0, label=r'$P_{iDC}$', color='lightcoral')
    ax2.plot(i_vals, p_vco_cnt[:len(i_vals)], linestyle='-.', linewidth=2.0, label=r'$P_{VCO} + P_{CNT}$', color='darkorange')
    ax2.plot(i_vals, ptot_vals[:len(i_vals)], linestyle='--', linewidth=2.0, label=r'$P_{TOT}$', color='coral')
    ax2.plot(result.input.i_dc_uA, result.output.P_tot_uW, 'ko', markersize=6, zorder=5)
    ax2.set_ylabel(r'$P_{TOT}$ (μW)', color='coral')
    ax2.tick_params(axis='y', labelcolor='coral')

    lines1, labels1 = ax.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax.legend(lines1 + lines2, labels1 + labels2, fontsize=9, loc='best')

def plot_forward_output_summary(ax, result, model):
    ax.axis("off")
    max_i_dc = model.i_dc_max(result.input.G_uS)

    txt = (
        f"ΔG:        {result.output.delta_G_uS * 1000:.4f} nS\n"
        f"P_VCO_CNT:     {result.output.P_cnt_uW + result.output.P_vco_uW:.4f} μW\n"
        f"P_idc:     {result.output.P_idc_uW:.4f} μW\n"
        f"P_TOT:     {result.output.P_tot_uW:.4f} μW\n"
        f"─────────────\n"
        f"i_dc_max: {max_i_dc:.4f} μA\nG×(V_dd-V_min)"
    )
        
    ax.text(
        0.5, 0.5, txt,
        transform=ax.transAxes,
        ha='center', va='center',
        fontsize=12,
        family='monospace',
        bbox=dict(boxstyle='round', facecolor='lightblue', alpha=0.25)
    )
    ax.set_title("Outputs values", fontweight='bold')