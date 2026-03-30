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
i_A_vals            = np.arange(0, 255) * iDAC_LSB_A
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

def calculate_resolution_bits(G, fs, idc, gpp, f_nyq):
    v_high = np.clip(Vdd_V - idc / (G + gpp/2), 0.2, 0.85)
    v_low = np.clip(Vdd_V - idc / (G - gpp/2), 0.2, 0.85)
    V_inputRange = v_high- v_low

    noise = vco.predict((v_high-v_low)/2 + v_low, fs_adc=fs)['ire']

    power_signal = V_inputRange**2
    power_noise  = noise**2

    snr =  10 * np.log10(power_signal / power_noise) + 10*np.log10(fs/f_nyq)
    n = (snr - 1.7)/6
    return n


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
P_idac = (Vdd_V * I1) + 400e-9
Power_W = p_res['p_tot'] / 1e6 + P_idac
op_Power = vco.predict(Vdd_V - op_A/op_S, fs_adc=op_sps, mode='single')['p_tot']/1e6 + (Vdd_V - op_A/op_S) * op_A

# SNDR Calculation
G5, FS5 = np.meshgrid(g_S_vals, fs_Hz_vals)
# resolution_grid = calculate_sndr_revised(G5, FS5, op_A, Gpp, f_nyquist)
resolution_grid = calculate_resolution_bits(G5, FS5, op_A, Gpp, f_nyquist)
resolution_grid[ np.isnan(resolution_grid) ] = 0
op_resolution = calculate_resolution_bits(op_S, op_sps, op_A, Gpp, f_nyquist)

# Duration Calculation
total_bits = mem_MB * 1024 * 1024 * 8
fs_curve = fs_Hz_vals
duration_hrs = total_bits / (fs_curve * bitwidth) / 3600
op_dur = total_bits / (op_sps * bitwidth) / 3600

fss = [0.1, 0.16237767391887217, 0.26366508987303583, 0.42813323987193935, 0.6951927961775606, 1.1288378916846888, 1.8329807108324356, 2.9763514416313175, 4.832930238571752, 7.847599703514611, 12.742749857031335, 20.6913808111479, 33.59818286283781, 54.555947811685144, 88.58667904100822, 143.8449888287663, 233.57214690901213, 379.2690190732246, 615.8482110660261, 1000.0]
nmses_fr = [17.411405732191106, 17.696769678491272, 19.133875433534723, 19.69616874580352, 20.54283866192327, 21.83441810153818, 22.774094702792755, 23.7921315513089, 25.061899510039215, 27.20197529760638, 26.8655814299075, 28.677913836016767, 29.256815482538563, 30.09440097733767, 31.290307535028937, 33.550774478876306, 34.17768961818983, 37.082997095213635, 38.62059194407533, 42.179347393374236]
nmses_lc = [16.20734396084577, 16.53710637272771, 17.523948380743345, 17.64160945990187, 19.526907532526774, 20.310816189097753, 20.057139930109145, 21.249467549207594, 22.545239678657943, 22.23197775424678, 22.377337088128787, 23.766698310274794, 22.902900195136766, 22.87268055183806, 24.037978680115977, 25.15547611455115, 27.221498633003506, 28.775173813780018, 29.278367479589946, 29.44988209919393]
crs = [0.4865036776233278, 0.6225672989486135, 0.8341439045095919, 1.1981190903700687, 1.5955497277893766, 2.3082084894265433, 3.2124554842523834, 4.330403788294516, 6.027265621518184, 8.023249569626108, 11.004624827092638, 16.848765254471196, 24.113608731721264, 34.63366982952229, 48.08652062625746, 62.04220395478713, 77.70670302356042, 90.77333246195502, 101.72383603532505, 110.3040732274068]

intrp_cr = np.interp(x=fs_Hz_vals, xp=fss, fp=crs )
duration_lc_hrs = duration_hrs*intrp_cr
op_dur_lc = op_dur*np.interp(x=[op_sps], xp=fss, fp=crs )[0]

intrp_nmse_fr = np.interp(x=fs_Hz_vals, xp=fss, fp=nmses_fr )
intrp_nmse_lc = np.interp(x=fs_Hz_vals, xp=fss, fp=nmses_lc )

fast = 0

# --- Plotting ---
plt.rcParams.update({'font.size': 9, 'font.family': 'serif'})
fig, axs = plt.subplots(2, 3, figsize=(12, 6))
formatter_u = FuncFormatter(lambda x, pos: f"{x * 1e6:g}")

# Helper for Marking OP
def mark_op(ax, x, y, stop=False):
    ax.axhline(y, linestyle='--', color='k', linewidth=0.8, alpha=0.7)
    ax.axvline(x, linestyle='--', color='k', linewidth=0.8, alpha=0.7)
    ax.scatter(x, y, color='k', s=15, zorder=5)

# 1. Sensitivity i vs G
if fast:
    im1 = axs[0, 0].imshow(S1, aspect='auto', origin='lower', cmap='jet_r',
                        extent=[g_S_vals.min(), g_S_vals.max(), i_A_vals.min(), i_A_vals.max()],
                        norm=LogNorm(vmin=dg_min, vmax=dg_max))
else:
    im1 = axs[0, 0].pcolormesh(g_S_vals,i_A_vals, S1,
                          shading='auto', cmap='jet_r',norm=LogNorm(vmin=dg_min, vmax=dg_max))
axs[0, 0].set_xscale('log')
axs[0, 0].xaxis.set_major_formatter(formatter_u)
axs[0, 0].yaxis.set_major_formatter(formatter_u)
axs[0, 0].set_ylabel(r"$i_{DC}\,\mathrm{(µA)}$", labelpad=-5)
axs[0, 0].set_xlabel(r"$G\,\mathrm{(µS)}$")
axs[0, 0].set_title(f"for $f_s = {op_sps:g}$ Hz", fontsize=10, pad=5)
mark_op(axs[0, 0], op_S, op_A)

# 2. Sensitivity fs vs G
if fast:
    im2 = axs[0, 1].imshow(S2, aspect='auto', origin='lower', cmap='jet_r',
                    extent=[g_S_vals.min(), g_S_vals.max(), fs_Hz_vals.min(), fs_Hz_vals.max()],
                    norm=LogNorm(vmin=dg_min, vmax=dg_max))
else:
    im2 = axs[0, 1].pcolormesh(g_S_vals, fs_Hz_vals, S2,
                          shading='auto', cmap='jet_r',norm=LogNorm(vmin=dg_min, vmax=dg_max))
axs[0, 1].set_xscale('log')
axs[0, 1].set_yscale('log')
axs[0, 1].xaxis.set_major_formatter(formatter_u)
axs[0, 1].yaxis.set_major_formatter(FuncFormatter(lambda x, pos: f"{x:g}"))
axs[0, 1].set_xlabel(r"$G\,\mathrm{(µS)}$")
axs[0, 1].set_ylabel(r"$f_{s}\,\mathrm{(Hz)}$", labelpad=-5)
axs[0, 1].set_title(f"for $i_{{DC}} = {op_A*1e6:g}$ µA", fontsize=10, pad=5)
mark_op(axs[0, 1], op_S, op_sps)

# 3. Sensitivity fs vs i
if fast:
    im3 = axs[0, 2].imshow(S3, aspect='auto', origin='lower', cmap='jet_r',
                    extent=[i_A_vals.min(), i_A_vals.max(), fs_Hz_vals.min(), fs_Hz_vals.max()],
                    norm=LogNorm(vmin=dg_min, vmax=dg_max))
else:
    im3 = axs[0, 2].pcolormesh(i_A_vals, fs_Hz_vals, S3,
                          shading='auto', cmap='jet_r',norm=LogNorm(vmin=dg_min, vmax=dg_max))

axs[0, 2].set_yscale('log')
axs[0, 2].xaxis.set_major_formatter(formatter_u)
axs[0, 2].yaxis.set_major_formatter(FuncFormatter(lambda x, pos: f"{x:g}"))
axs[0, 2].set_xlabel(r"$i_{DC}\,\mathrm{(µA)}$", labelpad=-5)
axs[0, 2].set_ylabel(r"$f_{s}\,\mathrm{(Hz)}$", labelpad=-5)
axs[0, 2].set_title(f"for $G = {op_S*1e6:g}$ µS", fontsize=10, pad=5)
mark_op(axs[0, 2], op_A, op_sps)

# 4. Power i vs G
if fast:
    im4 = axs[1, 0].imshow(Power_W*1e6, aspect='auto', origin='lower', cmap='jet',
                       extent=[g_S_vals.min(), g_S_vals.max(), i_A_vals.min(), i_A_vals.max()])
else:
    im4 = axs[1, 0].pcolormesh(g_S_vals, i_A_vals, Power_W*1e6,
                          shading='auto', cmap='jet')
axs[1, 0].set_xscale('log')
axs[1, 0].xaxis.set_major_formatter(formatter_u)
axs[1, 0].yaxis.set_major_formatter(formatter_u)
axs[1, 0].set_ylabel(r"$i_{DC}\,\mathrm{(µA)}$", labelpad=-5)
axs[1, 0].set_xlabel(r"$G\,\mathrm{(µS)}$")
axs[1, 0].set_title(f"Sensing Power (iDAC + VCO-ADC)", fontsize=10)
# axs[1, 0].set_title(f"Total Power = {op_Power[0]*1e6:0.2f} µW\nfor $f_s = {op_sps:g} Hz$", fontsize=10)
mark_op(axs[1, 0], op_S, op_A)
# fig.colorbar(im4, ax=axs[1, 0], label="Power (W)")

edge_y = []
edge_x = []
for iidx, i in enumerate(i_A_vals):
    not_valid = S1[iidx] >= dg_max
    if len(not_valid >0):
        for gidx, x in enumerate(not_valid):
            if not x: break
        edge_x.append(g_S_vals[gidx])
        edge_y.append(i_A_vals[iidx])
axs[1, 0].plot(edge_x, edge_y, '--k')

# 5. SNR fs vs G
if fast:
    im5 = axs[1, 1].imshow(resolution_grid, aspect='auto', origin='lower', cmap='jet',
                        extent=[g_S_vals.min(), g_S_vals.max(), fs_Hz_vals.min(), fs_Hz_vals.max()])
else:
    im5 = axs[1, 1].pcolormesh(g_S_vals, fs_Hz_vals, resolution_grid,
                          shading='auto', cmap='jet')
axs[1, 1].set_xscale('log')
axs[1, 1].set_yscale('log')
axs[1, 1].xaxis.set_major_formatter(formatter_u)
axs[1, 1].yaxis.set_major_formatter(FuncFormatter(lambda x, pos: f"{x:g}"))
axs[1, 1].set_xlabel(r"$G\,\mathrm{(µS)}$")
axs[1, 1].set_ylabel(r"$f_{s}\,\mathrm{(Hz)}$", labelpad=-5)

# axs[1, 1].set_title(f"SNR = {op_resolution[0]:0.1f} dB\nfor $G_{{pp}}={Gpp*1e6:g}$µS, $f_{{nyq}}={f_nyquist}$Hz", fontsize=10)
axs[1, 1].set_title(f"Resolution for $G_{{pp}}={Gpp*1e6:g}$µS, $f_{{nyq}}={f_nyquist}$Hz", fontsize=10)
mark_op(axs[1, 1], op_S, op_sps)
# fig.colorbar(im5, ax=axs[1, 1], label="SNDR (dB)")

# 6. Duration
im6 = axs[1, 2].get_position()
axs[1, 2].set_position([im6.x0, im6.y0, im6.width * 0.2, im6.height])
axs[1, 2].plot(duration_hrs, fs_curve, color='gray', lw=2, label="Fixed rate (16b)")
axs[1, 2].plot(duration_lc_hrs, fs_curve, color='black', lw=2, label="Event-based (8b)")
axs[1, 2].set_yscale('log')
axs[1, 2].set_xscale('log')
axs[1, 2].xaxis.set_major_formatter(FuncFormatter(lambda x, pos: f"{x:g}"))
axs[1, 2].yaxis.set_major_formatter(FuncFormatter(lambda x, pos: f"{x:g}"))
axs[1, 2].set_ylim(fs_Hz_vals.min(), fs_Hz_vals.max())
axs[1, 2].set_xlabel(f"― Recording Duration (hours)\n{mem_MB*1000:g}KB @ bits/sample")
axs[1, 2].set_ylabel(r"$f_{s}\,\mathrm{(Hz)}$", labelpad=-5)
axs[1, 2].legend(loc='lower left')
axs[1, 2].grid(True, which='both', alpha=0.3)
mark_op(axs[1, 2], op_dur, op_sps, stop=True)
mark_op(axs[1, 2], op_dur_lc, op_sps, stop=True)

axs12_r = axs[1, 2].twiny()
axs12_r.plot(-intrp_nmse_fr,fs_curve, linestyle=':',color='gray')
axs12_r.plot(-intrp_nmse_lc,fs_curve, linestyle=':',color='black')
axs12_r.xaxis.set_major_formatter(FuncFormatter(lambda x, pos: f"{x:g}"))
axs12_r.xaxis.set_ticks([-20,-25,-30])
axs12_r.set_xlabel("··· NRMSE (dB)")

# Top Colorbar & Main Title
# fig.text(0.48, 0.94, f"Sensitivity $\Delta$ G = {op_dS[0]*1e9:1.2f} nS" , ha='center', fontsize=12, fontweight='bold')

bar_pos = 0.03
fig.subplots_adjust(top=0.88, right=0.92)
cax_top = fig.add_axes([bar_pos, 0.55, 0.015, 0.33])
cbar = fig.colorbar(im1, cax=cax_top, label=r"Sensitivity $\Delta G$ (S)")
# cbar.ax.yaxis.set_ticks_position('left')
# cbar.ax.yaxis.set_label_position('left')

cax_bot = fig.add_axes([bar_pos, 0.1, 0.015, 0.33])
fig.colorbar(im5, cax=cax_bot, label="Resolution (bits)")
cax_bot = fig.add_axes([bar_pos+0.0001, 0.1, 0.015, 0.33])
cbar = fig.colorbar(im4, cax=cax_bot)
cbar.set_label("Power (µW)", labelpad=-5)
cbar.ax.yaxis.set_ticks_position('left')
cbar.ax.yaxis.set_label_position('left')
cbar.set_ticks([1, 10])
cbar.set_ticklabels(['1', '10'])
cbar.ax.tick_params(pad=2)  # default is usually ~4–6


plt.tight_layout(rect=[0.09, 0, 1, 0.95], w_pad=0)
plt.savefig('figs/Trade-offs.png', dpi=600)
plt.show()
#In[]:

plt.savefig('figs/Trade-offs.svg')

#In[]:

print(f"G = {op_S*1e6} µS")
print(f"ΔG = {op_dS[0]*1e9:1.1f} nS")
print(f"fs = {op_sps} Hz")
print(f"iDC = {op_A*1e6:1.1f} µA")
print(f"Ptot= {op_Power[0]*1e6:1.1f} µW")
print(f"SNR = {op_resolution[0]:1.1f} dB")
print(f"dur FR = {op_dur*60:1.0f} min")
print(f"dur LC = {op_dur_lc*60:1.0f} min")
