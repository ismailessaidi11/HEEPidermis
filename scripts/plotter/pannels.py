import numpy as np
import matplotlib.pyplot as plt

import numpy as np
import matplotlib.pyplot as plt


def plot_fosc_model(VCO_model, mode="fit", ax=None, result=None, show_stats=True):
    """
    mode : str, default="fit"
        Plot mode:
        - "fit": show fit curve + residuals + optional printed stats
        - "measurement": show model + optional measured operating point
    """
    vin_data = VCO_model.vin_data
    fosc_data = VCO_model.fosc_data

    if mode == "fit":
        fig, ax = plt.subplots(figsize=(10, 6))
        # Common measured data scatter
        ax.scatter(
            vin_data, fosc_data,
            s=80 if mode == "fit" else 60,
            color="black",
            alpha=0.7 if mode == "fit" else 0.6,
            label="Measured data",
            zorder=5
        )
        vin_plot = np.linspace(225, 820, 300)
        fosc_plot = VCO_model.fosc_from_vin(vin_plot)
        fosc_points = VCO_model.fosc_from_vin(vin_data)

        ax.plot(
            vin_plot, fosc_plot,
            color="purple",
            linewidth=2.5,
            label="Piecewise polynomial fit"
        )

        residuals = fosc_data - fosc_points
        ax.scatter(
            vin_data, residuals,
            s=50,
            color="orange",
            alpha=0.6,
            label="Residuals"
        )
        ax.axhline(y=0, color="k", linestyle="-", linewidth=0.5)

        ax.set_xlabel("V_in (mV)", fontsize=12)
        ax.set_ylabel("f_osc (kHz) / Residuals", fontsize=12)
        ax.set_title("Piecewise Polynomial Model Fit", fontsize=13)

        if show_stats:
            print(f"\n{'='*60}")
            print("Piecewise Polynomial Model")
            print(f"{'='*60}")
            print(f"Piecewise threshold: {VCO_model.piecewise_threshold:.2f} mV")

            print("\nPolynomial coefficients (active region only):")
            a, b, c = VCO_model.popt_poly
            print(f"  a = {a:.6f}")
            print(f"  b = {b:.4f}")
            print(f"  c = {c:.3f}")

            print(f"\nEquation: f_osc = {a:.6f} × V_in² + {b:.4f} × V_in + {c:.3f}")
            print(f"          (valid for V_in ≥ {VCO_model.piecewise_threshold:.2f} mV)")

            residuals = fosc_data - VCO_model.fosc_from_vin(vin_data)
            rmse = np.sqrt(np.mean(residuals**2))
            mae = np.mean(np.abs(residuals))

            print("\nFit Statistics (all data):")
            print(f"  RMSE: {rmse:.2f} kHz")
            print(f"  MAE:  {mae:.2f} kHz")

            active_mask = vin_data >= VCO_model.piecewise_threshold
            residuals_active = residuals[active_mask]
            rmse_active = np.sqrt(np.mean(residuals_active**2))
            mae_active = np.mean(np.abs(residuals_active))

            print(f"\nFit Statistics (active region only, V_in ≥ {VCO_model.piecewise_threshold:.2f} mV):")
            print(f"  RMSE: {rmse_active:.2f} kHz")
            print(f"  MAE:  {mae_active:.2f} kHz")
            print(f"{'='*60}\n")
        plt.tight_layout()
        plt.show()

    elif mode == "measurement":
        vin_plot = VCO_model.params.vin_range
        fosc_plot = VCO_model.fosc_from_vin(vin_plot)
    # Common measured data scatter
        ax.scatter(
            vin_data, fosc_data,
            s=80 if mode == "fit" else 60,
            color="black",
            alpha=0.7 if mode == "fit" else 0.6,
            label="Measured data",
            zorder=5
        )
        ax.plot(
            vin_plot, fosc_plot,
            "b-",
            linewidth=2.5,
            label="Piecewise polynomial model"
        )

        if result is not None and not np.isnan(result.vin_mV) and 225 <= result.vin_mV <= 820:
            ax.plot(
                result.vin_mV,
                result.f_osc_measured_kHz,
                "r*",
                markersize=20,
                label=f"Measured: f={result.f_osc_measured_kHz:.0f} kHz",
                zorder=10
            )
            ax.axvline(result.vin_mV, color="r", linestyle="--", alpha=0.5)

        ax.set_xlabel("V_in (mV)", fontsize=11)
        ax.set_ylabel("f_osc (kHz)", fontsize=11)
        ax.set_title("f_osc Model & Measurement", fontsize=12, fontweight="bold")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=10)
        ax.set_xlim(200, 850)
    else:
        raise ValueError("mode must be either 'fit' or 'measurement'")

def plot_vin_text(ax, VCO_model, result): 
    # ===== Panel 2: V_in from f_osc measurement =====
    ax.axis('off')

    # Title
    ax.text(0.5, 0.9,'Step 1: Extract V_in from f_osc',transform=ax.transAxes,fontsize=13,
        fontweight='bold',ha='center')

    if not np.isnan(result.vin_mV):
        vin_text = f"{result.vin_mV:.2f} mV"
        status = "✓ Valid operating region"
        color = "lightgreen"
    else:
        vin_text = "Invalid"
        status = "✗ Outside oscillator range"
        color = "salmon"

    ax.text(0.5, 0.60, vin_text, transform=ax.transAxes, fontsize=15, ha='center',
            bbox=dict(boxstyle='round', facecolor=color, alpha=0.7))

    info_text = (
        f"Measured f_osc: {result.f_osc_measured_kHz:.1f} kHz\n"
        f"Model threshold: {VCO_model.piecewise_threshold:.1f} mV\n"
        f"{status}"
    )

    ax.text(0.5, 0.35,info_text,transform=ax.transAxes,fontsize=11,ha='center',family='monospace')

def plot_conductance_text(ax, result): 
    ax.text(0.5, 0.85, 'Step 2: Calculate G for given i_dc', transform=ax.transAxes, 
            fontsize=12, fontweight='bold', ha='center')
    
    calc_text = f"i_dc: {result.i_dc_uA:.2f} μA\n"
    calc_text += f"V_in: {result.vin_mV:.2f} mV\n"
    calc_text += f"G = i_dc / (VDD - V_in)\n"
    calc_text += f"─────────────────\n"
    
    if not np.isnan(result.G_uS):
        calc_text += f"\nG: {result.G_uS:.4f} uS\n"
        calc_text += f"R: {result.R_kohm:.2f} kΩ"
    else:
        calc_text += f"\n✗ Cannot calculate"
    
    ax.text(0.5, 0.45, calc_text, transform=ax.transAxes, fontsize=12,
            ha='center', va='center', bbox=dict(boxstyle='round', facecolor='lightgreen', alpha=0.8),
            family='monospace')
    ax.axis('off')
        
def plot_G_vs_idc(ax, result): 
    ax.plot(result.i_dc_range, result.G_curve if not np.isnan(result.vin_mV) else [], 'g-', linewidth=2.5, label=f'V_in = {result.vin_mV:.1f} mV')
    if not np.isnan(result.vin_mV):
        ax.plot(result.i_dc_uA, result.G_uS, 'r*', markersize=20, label=f'Selected i_dc = {result.i_dc_uA:.2f} μA', zorder=5)

    ax.set_xlabel('i_dc (μA)', fontsize=11)
    ax.set_ylabel('G (uS)', fontsize=11)
    ax.set_title('G vs i_dc at fixed V_in', fontsize=12, fontweight='bold')
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=10)

def plot_dGdf(ax, result): 
    # Calculate delta G/d(f_osc) at the measured f_osc point for different i_dc values

    ax.plot(result.i_dc_range, result.dG_df_curve, 'purple', linewidth=3, label='G Tolerance: dG/d(f_osc) [uS/kHz]')
    ax.plot(result.i_dc_uA, result.dG_df_curve[np.argmin(np.abs(result.i_dc_range - result.i_dc_uA))], 'r*', 
            markersize=20, label=f'Current point ({result.i_dc_uA:.2f} μA, {result.dG_df:.4f} uS)', zorder=5)
    ax.fill_between(result.i_dc_range, result.dG_df_curve, alpha=0.3, color='purple')
    
    ax.set_xlabel('i_dc (μA)', fontsize=11)
    ax.set_ylabel('G Tolerance [uS/kHz]', fontsize=11)
    ax.set_title('Step 3: How much does G vary with f_osc measurement error?', 
                    fontsize=12, fontweight='bold')
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=10)

def plot_delta_G(ax, model, result, fs_Hz):
    fs_ref = [0.5, 1.0, 5.0, 10.0]
    
    for fs in fs_ref:
        delta_G_curve = model.delta_G(result.vin_mV, result.i_dc_range, fs_Hz=fs)
        style = '--' if fs != fs_Hz else '-'
        lw = 1.5 if fs != fs_Hz else 3
        alpha = 0.6 if fs != fs_Hz else 1.0
        ax.plot(
            result.i_dc_range,
            delta_G_curve,
            linestyle=style,
            linewidth=lw,
            alpha=alpha,
            label=f'f_s = {fs:g} Hz'
        )

    current_curve = model.delta_G(result.vin_mV, result.i_dc_range, fs_Hz=fs_Hz)
    current_delta_G = model.delta_G(result.vin_mV, result.i_dc_uA, fs_Hz=fs_Hz)

    ax.fill_between(result.i_dc_range, current_curve, alpha=0.2)
    ax.plot(result.i_dc_uA, current_delta_G, 'r*', markersize=18,
            label=f'Current point ({result.i_dc_uA:.2f} μA, {current_delta_G:.4f} uS)',
            zorder=5)

    ax.set_xlabel('i_dc (μA)', fontsize=11)
    ax.set_ylabel('Estimated ΔG (uS)', fontsize=11)
    ax.set_title('ΔG vs i_dc for different sampling frequencies', fontsize=12, fontweight='bold')
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=9)

def plot_delta_G_heatmap(ax, model, result, fs_Hz):
    fs_vals = np.linspace(0.5, 20, 100)
    idc_vals = result.i_dc_range

    Z = np.array([
        model.delta_G(result.vin_mV, idc_vals, fs_Hz=fs)
        for fs in fs_vals
    ])

    im = ax.imshow(
        Z,
        aspect='auto',
        origin='lower',
        extent=[idc_vals.min(), idc_vals.max(), fs_vals.min(), fs_vals.max()]
    )

    ax.plot(result.i_dc_uA, fs_Hz, 'r*', markersize=14)
    ax.set_xlabel('i_dc (μA)')
    ax.set_ylabel('f_s (Hz)')
    ax.set_title('ΔG(i_dc, f_s)')

def print_analysis(model, result):
    print("\n" + "="*70)
    print("MEASUREMENT WORKFLOW ANALYSIS")
    print("="*70)
    print(f"\n1. MEASUREMENT INPUT:")
    print(f"   f_osc (measured):  {result.f_osc_measured_kHz:.1f} kHz")
    print(f"   Model used:        Piecewise Polynomial")
    print(f"   Threshold:         {model.piecewise_threshold:.2f} mV")
    
    print(f"\n2. EXTRACTED V_IN:")
    if not np.isnan(result.vin_mV):
        print(f"   V_in:              {result.vin_mV:.3f} mV")
    else:
        print(f"   V_in:              ✗ OUT OF RANGE")
    
    print(f"\n3. CONDUCTANCE CALCULATION:")
    print(f"   i_dc (applied):    {result.i_dc_uA:.3f} μA")
    if not np.isnan(result.G_uS):
        print(f"   G:                 {result.G_uS:.6f} uS")
        print(f"   R:                 {result.R_kohm:.4f} kΩ")
    else:
        print(f"   G:                 ✗ CANNOT CALCULATE")
    # G tolerance analysis
    print(f"\n4. G TOLERANCE ANALYSIS:")
    current_dG_df = result.dG_df_curve[np.argmin(np.abs(result.i_dc_range - result.i_dc_uA))]
    print(f"   dG/d(f_osc):       {current_dG_df:.6f} uS/kHz")
    print(f"   Interpretation:    1 kHz error in f_osc → {current_dG_df:.6f} uS error in G")

    max_tolerance_idx = np.argmax(result.dG_df_curve)
    print(f"\n   Lowest resolution at: {result.i_dc_range[max_tolerance_idx]:.2f} μA ({result.dG_df_curve[max_tolerance_idx]:.6f} uS/kHz)")
    min_tolerance_idx = np.argmin(np.array(result.dG_df_curve)[np.array(result.dG_df_curve) > 0]) if any(t > 0 for t in result.dG_df_curve) else 0
    if result.dG_df_curve[min_tolerance_idx] > 0:
        print(f"   Highest resolution at: {result.i_dc_range[min_tolerance_idx]:.2f} μA ({result.dG_df_curve[min_tolerance_idx]:.6f} uS/kHz)")
    
    print("\n" + "="*70 + "\n")

def plot_power(ax, model, result):
    vin = result.vin_mV

    ax.scatter(model.vin_data, model.pvco_data, label="P_VCO", s=50)
    ax.scatter(model.vin_data, model.pcnt_data, label="P_Counter", s=50)

    # Current operating Vin
    ax.axvline(vin, color='r', linestyle='--', alpha=0.6, label=f'Current V_in = {vin:.1f} mV')

    # Nearest measured point
    idx = np.argmin(np.abs(model.vin_data - vin))
    vin_nearest = model.vin_data[idx]
    pvco = model.pvco_data[idx]
    pcnt = model.pcnt_data[idx]

    ax.plot(vin_nearest, pvco, 'o', markersize=10, color='red', zorder=5)
    ax.plot(vin_nearest, pcnt, 's', markersize=10, color='red', zorder=5)

    text = (
        f"Nearest measured V_in = {vin_nearest:.1f} mV\n"
        f"P_VCO = {pvco:.3f} µW\n"
        f"P_Counter = {pcnt:.3f} µW\n"
        f"P_current_injection = {result.skin_power_uW:.3f} µW\n"
        f"P_Total = {pvco + pcnt + result.skin_power_uW:.3f} µW"
    )

    ax.text(
        0.02, 0.98, text,
        transform=ax.transAxes,
        va='top',
        ha='left',
        fontsize=10,
        bbox=dict(boxstyle='round', facecolor='white', alpha=0.85)
    )

    ax.set_xlabel("V_in (mV)", fontsize=11)
    ax.set_ylabel("Power (µW)", fontsize=11)
    ax.set_title("Power Measurements vs V_in", fontsize=12, fontweight="bold")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=9)