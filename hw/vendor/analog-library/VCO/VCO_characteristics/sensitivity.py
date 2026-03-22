#In[]:
# Delta G
%matplotlib inline

import matplotlib as mpl
import matplotlib.cm as cm
import matplotlib.colors as colors
import matplotlib.pyplot as plt
from scipy.interpolate import interp1d
from matplotlib.colors import LogNorm
from matplotlib.ticker import ScalarFormatter, LogFormatterSciNotation, FuncFormatter
import numpy as np
from vco_model import VCOADCModel

#In[]
%matplotlib inline

vco = VCOADCModel(data_folder='data')

# --- Constants & OPs ---
Vdd_V               = 0.8
iDAC_LSB_A          = 40e-9
op_S                = 19e-6
op_A                = iDAC_LSB_A * 26
op_sps              = 20
include_variance    = 3

Gpp                 = 1e-6
f_nyquist           = 1
bitwidth            = 16
mem_MB              = 0.016

g_S_vals            = np.linspace(1e-6, 100e-6, 1000)
i_A_vals            = np.arange(0, 256) * iDAC_LSB_A
fs_Hz_vals          = np.logspace(-1, 2, 1000)

# Force a minimum and maximum range of sensitivity
if include_variance:
    dg_min = 1e-9
    dg_max = 1e-4  # since you clip above this
else:
    dg_min = 1e-12
    dg_max = 1e-4  # since you clip above this


# --- Functions ---
def dg_S_func(g_S, fs_sps, i_A, variance=1):
    Vin_V = (Vdd_V - i_A/g_S)
    res = vco.predict(Vin_V, fs_adc=fs_sps)
    fosc_Hz = res['f_osc']
    dfosc_Hz = (fosc_Hz * fs_sps) / (fosc_Hz + fs_sps)
    if variance:
        dfosc_error_Hz = variance * res['f_osc_error']
        dfosc_max_Hz = np.maximum(dfosc_Hz, dfosc_error_Hz)
    else:
        dfosc_max_Hz = dfosc_Hz
    dVin_max_V = dfosc_max_Hz / res["k"]
    dg = (g_S * dVin_max_V) / (Vdd_V - Vin_V + dVin_max_V)
    return np.abs(dg)

def calculate_sndr_revised(g_centers, fs_sweep, idc, gpp, f_nyq):
    v_high = np.clip(Vdd_V - idc / (g_centers + gpp/2), 0.2, 0.85)
    v_low = np.clip(Vdd_V - idc / (g_centers - gpp/2), 0.2, 0.85)
    res_high = vco.predict(v_high, fs_adc=fs_sweep)
    res_low = vco.predict(v_low, fs_adc=fs_sweep)
    f_swing_pp = np.abs(res_high['f_osc'] - res_low['f_osc'])
    p_signal = (f_swing_pp / 2)**2 / 2
    p_noise_base = (res_high['f_osc_error'])**2
    noise_factor = (2 * f_nyq) / fs_sweep
    p_noise_effective = p_noise_base * noise_factor
    p_dist = 1e-12
    sndr_db = 10 * np.log10(p_signal / (p_noise_effective + p_dist))
    return np.clip(sndr_db, -10, 100)

# Sensitivity Calculations
G1, I1  = np.meshgrid(g_S_vals, i_A_vals)
S1      = dg_S_func(G1, op_sps, I1, include_variance)
G2, F2  = np.meshgrid(g_S_vals, fs_Hz_vals)
S2      = dg_S_func(G2, F2, op_A, include_variance)
I3, F3  = np.meshgrid(i_A_vals, fs_Hz_vals)
S3      = dg_S_func(op_S, F3, I3, include_variance)

S1[ S1 > dg_max] = dg_max
S2[ S2 > dg_max] = dg_max
S3[ S3 > dg_max] = dg_max
S1[ np.isnan(S1)] = dg_max
S2[ np.isnan(S2)] = dg_max
S3[ np.isnan(S3)] = dg_max

# OP Sensitivity
op_dS = dg_S_func(op_S, op_sps, op_A, include_variance)

# Power Calculation
Vin_pwr = np.clip(Vdd_V - I1 / G1, 0.2, 0.85)
p_res = vco.predict(Vin_pwr, fs_adc=op_sps)
Power_W = p_res['p_tot'] / 1e6 + (Vin_pwr * I1)
op_Power = vco.predict(Vdd_V - op_A/op_S, fs_adc=op_sps, mode='single')['p_tot']/1e6 + (Vdd_V - op_A/op_S) * op_A

# SNDR Calculation
G5, FS5 = np.meshgrid(g_S_vals, fs_Hz_vals)
SNDR_grid = calculate_sndr_revised(G5, FS5, op_A, Gpp, f_nyquist)
op_SNDR = calculate_sndr_revised(op_S, op_sps, op_A, Gpp, f_nyquist)

# Duration Calculation
total_bits = mem_MB * 1024 * 1024 * 8
fs_curve = fs_Hz_vals
duration_hrs = total_bits / (fs_curve * bitwidth) / 3600
op_dur = total_bits / (op_sps * bitwidth) / 3600

# --- Plotting ---
plt.rcParams.update({'font.size': 9, 'font.family': 'serif'})
fig, axs = plt.subplots(2, 3, figsize=(12, 6))
formatter_u = FuncFormatter(lambda x, pos: f"{x * 1e6:g}")

# Helper for Marking OP
def mark_op(ax, x, y):
    ax.axhline(y, linestyle='--', color='k', linewidth=0.8, alpha=0.7)
    ax.axvline(x, linestyle='--', color='k', linewidth=0.8, alpha=0.7)
    ax.scatter(x, y, color='k', s=15, zorder=5)

# 1. Sensitivity i vs G
im1 = axs[0, 0].imshow(S1, aspect='auto', origin='lower', cmap='jet_r',
                       extent=[g_S_vals.min(), g_S_vals.max(), i_A_vals.min(), i_A_vals.max()],
                       norm=LogNorm(vmin=dg_min, vmax=dg_max))
axs[0, 0].set_xscale('log')
axs[0, 0].xaxis.set_major_formatter(formatter_u)
axs[0, 0].yaxis.set_major_formatter(formatter_u)
axs[0, 0].set_ylabel(r"$i_{DC}\,\mathrm{(µA)}$")
axs[0, 0].set_xlabel(r"$G\,\mathrm{(µS)}$")
axs[0, 0].set_title(f"for $f_s = {op_sps:g}$ Hz", fontsize=10, pad=5)
mark_op(axs[0, 0], op_S, op_A)

# 2. Sensitivity fs vs G
axs[0, 1].imshow(S2, aspect='auto', origin='lower', cmap='jet_r',
                 extent=[g_S_vals.min(), g_S_vals.max(), fs_Hz_vals.min(), fs_Hz_vals.max()],
                 norm=LogNorm(vmin=dg_min, vmax=dg_max))
axs[0, 1].set_xscale('log')
axs[0, 1].set_yscale('log')
axs[0, 1].xaxis.set_major_formatter(formatter_u)
axs[0, 1].set_xlabel(r"$G\,\mathrm{(µS)}$")
axs[0, 1].set_ylabel(r"$f_{s}\,\mathrm{(Hz)}$")
axs[0, 1].set_title(f"for $i_{{DC}} = {op_A*1e6:g}$ µA", fontsize=10, pad=5)
mark_op(axs[0, 1], op_S, op_sps)

# 3. Sensitivity fs vs i
axs[0, 2].imshow(S3, aspect='auto', origin='lower', cmap='jet_r',
                 extent=[i_A_vals.min(), i_A_vals.max(), fs_Hz_vals.min(), fs_Hz_vals.max()],
                 norm=LogNorm(vmin=dg_min, vmax=dg_max))
axs[0, 2].set_yscale('log')
axs[0, 2].xaxis.set_major_formatter(formatter_u)
axs[0, 2].set_xlabel(r"$i_{DC}\,\mathrm{(µA)}$")
axs[0, 2].set_ylabel(r"$f_{s}\,\mathrm{(Hz)}$")
axs[0, 2].set_title(f"for $G = {op_S*1e6:g}$ µS", fontsize=10, pad=5)
mark_op(axs[0, 2], op_A, op_sps)

# Top Colorbar & Main Title
bar_pos = 0.94
fig.subplots_adjust(top=0.88, right=0.92)
cax_top = fig.add_axes([bar_pos, 0.55, 0.015, 0.33])
fig.colorbar(im1, cax=cax_top, label=r"$\Delta G$ (S)")
fig.text(0.48, 0.94, f"Sensitivity $\Delta$ G = {op_dS[0]*1e9:1.2f} nS" , ha='center', fontsize=12, fontweight='bold')

# 4. Power i vs G
im4 = axs[1, 0].imshow(Power_W*1e6, aspect='auto', origin='lower', cmap='jet',
                       extent=[g_S_vals.min(), g_S_vals.max(), i_A_vals.min(), i_A_vals.max()],
                       norm=LogNorm())
axs[1, 0].set_xscale('log')
axs[1, 0].xaxis.set_major_formatter(formatter_u)
axs[1, 0].yaxis.set_major_formatter(formatter_u)
axs[1, 0].set_ylabel(r"$i_{DC}\,\mathrm{(µA)}$")
axs[1, 0].set_xlabel(r"$G\,\mathrm{(µS)}$")
axs[1, 0].set_title(f"Total Power = {op_Power[0]*1e6:0.2f} µW\nfor $f_s = {op_sps:g} Hz$", fontsize=10)
mark_op(axs[1, 0], op_S, op_A)
# fig.colorbar(im4, ax=axs[1, 0], label="Power (W)")

# 5. SNDR fs vs G
im5 = axs[1, 1].imshow(SNDR_grid, aspect='auto', origin='lower', cmap='jet',
                       extent=[g_S_vals.min(), g_S_vals.max(), fs_Hz_vals.min(), fs_Hz_vals.max()])
axs[1, 1].set_xscale('log')
axs[1, 1].set_yscale('log')
axs[1, 1].xaxis.set_major_formatter(formatter_u)
axs[1, 1].set_xlabel(r"$G\,\mathrm{(µS)}$")
axs[1, 1].set_ylabel(r"$f_{s}\,\mathrm{(Hz)}$")
axs[1, 1].set_title(f"SNDR = {op_SNDR[0]:0.1f} dB\nfor $G_{{pp}}={Gpp*1e6:g}$µS, $f_{{nyq}}={f_nyquist}$Hz", fontsize=10)
mark_op(axs[1, 1], op_S, op_sps)
# fig.colorbar(im5, ax=axs[1, 1], label="SNDR (dB)")

# 6. Duration
pos = axs[1, 2].get_position()
axs[1, 2].set_position([pos.x0, pos.y0, pos.width * 0.2, pos.height])
axs[1, 2].plot(duration_hrs, fs_curve, color='gray', lw=2)
axs[1, 2].set_yscale('log')
axs[1, 2].set_ylim(fs_Hz_vals.min(), fs_Hz_vals.max())
axs[1, 2].set_xlabel("Recording Duration (Hours)")
axs[1, 2].set_ylabel(r"$f_{s}\,\mathrm{(Hz)}$")
axs[1, 2].set_title(f"Duration = {op_dur:0.2f} h\nfor $MB = {mem_MB*1000:g}$KB, $b = {bitwidth}$", fontsize=10)
axs[1, 2].grid(True, which='both', alpha=0.3)
mark_op(axs[1, 2], op_dur, op_sps)

bar_pos = 0.94
cax_bot = fig.add_axes([bar_pos, 0.1, 0.015, 0.33])
fig.colorbar(im5, cax=cax_bot, label="SNDR (dB)")
cax_bot = fig.add_axes([bar_pos+0.0001, 0.1, 0.015, 0.33])
cbar = fig.colorbar(im4, cax=cax_bot)
cbar.set_label("Power (µW)", labelpad=-5)

# Move ticks and labels to the left
cbar.ax.yaxis.set_ticks_position('left')
cbar.ax.yaxis.set_label_position('left')
cbar.set_ticks([1, 10])
cbar.set_ticklabels(['1', '10'])
cbar.ax.tick_params(pad=2)  # default is usually ~4–6

plt.tight_layout(rect=[0, 0, 0.92, 0.94], w_pad=3)
plt.savefig('combined_analysis_6plots.png', dpi=200)
plt.show()