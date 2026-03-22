#In[]:
# Model the VCO
# The objective of this script is to model the behavior of the DAC + ADC system to measure GSR
# It takes the post-layout characterizations and sweeps various parameters to help choose the operational point (OP)

import pickle
import numpy as np
import matplotlib as mpl
import matplotlib.cm as cm
import matplotlib.colors as colors
import matplotlib.pyplot as plt
from scipy.interpolate import interp1d
from matplotlib.colors import LogNorm
from matplotlib.ticker import ScalarFormatter


TRANSFER_FILENAME = "VCO variability - transfer.csv"



# Load the values of sensitivity as a function of current and input voltage
with open("./VCO/in/DG_vs_iout_and_Vin.pkl",'rb') as f:
    (DG_S, DG_Vin_V, DG_iout_A) = pickle.load(f)

# Load the VCO's transfer function (fosc vs Vin)
with open("./VCO/in/fosc_Hz_vs_Vin_V_TT.pkl",'rb') as f:
    Vin_mV, fosc_kHz = pickle.load(f)
# Convert the transfer function to regular units
Vin_V   = Vin_mV/1e3
fosc_tt_Hz = fosc_kHz*1e3

# Load the VCO's transfer function (fosc vs Vin)
with open("./VCO/in/fosc_Hz_vs_Vin_V_FF.pkl",'rb') as f:
    Vin_ff_mV , fosc_kHz = pickle.load(f)
# Convert the transfer function to regular units
fosc_ff_Hz = fosc_kHz*1e3

# Load the VCO's transfer function (fosc vs Vin)
with open("./VCO/in/fosc_Hz_vs_Vin_V_SS.pkl",'rb') as f:
    Vin_ss_mV , fosc_kHz = pickle.load(f)
# Convert the transfer function to regular units
fosc_ss_Hz = fosc_kHz*1e3



Vin_ff_mV   = Vin_ff_mV[fosc_ff_Hz > 0][:-5]
fosc_ff_Hz  = fosc_ff_Hz[fosc_ff_Hz > 0][:-5]

fosc_ss_Hz = fosc_ss_Hz[:-5]
Vin_ss_mV = Vin_ss_mV[:-5]

plt.figure(figsize=(6,3))
plt.plot( Vin_V*1e3, fosc_tt_Hz/1e3, linestyle='-', color='black', label='TT')
plt.plot( Vin_ff_mV ,fosc_ff_Hz/1e3, linestyle='--', color='black', label='FF')
plt.plot( Vin_ss_mV, fosc_ss_Hz/1e3, linestyle=':', color='black', label='SS')
plt.ylabel("Input Voltage (mV)")
plt.xlabel("Oscillation Frequency (kHz)")
plt.grid(which='both')
plt.legend(loc='upper left')
# plt.xlim(-100,1100)
plt.tight_layout()
plt.savefig("./out/P_vco_cnt.png")
plt.show()
with open("./VCO/in/fosc_corners_Hz_vs_Vin_mV.pkl",'wb+') as f:
    pickle.dump(( Vin_V*1e3, fosc_tt_Hz,Vin_ff_mV,fosc_ff_Hz,Vin_ss_mV, fosc_ss_Hz ), f)

import csv

data = {
    "Vin_tt_mV": Vin_V*1e3,
    "fosc_tt_Hz": fosc_tt_Hz,
    "Vin_ff_mV": Vin_ff_mV,
    "fosc_ff_Hz": fosc_ff_Hz,
    "Vin_ss_mV": Vin_ss_mV,
    "fosc_ss_Hz": fosc_ss_Hz,
}


with open("./VCO/in/fosc_corners_Hz_vs_Vin_mV.csv", "w", newline='') as f:
    writer = csv.writer(f)
    # header
    writer.writerow(data.keys())
    # rows
    max_len = max(len(v) for v in data.values())
    for i in range(max_len):
        row = [data[k][i] if i < len(data[k]) else "" for k in data]
        writer.writerow(row)



# Define the supply (LDO) voltage
Vdd_V       = 0.8
# Define the iDAC's LSB
iDAC_LSB_A  = 40e-9


#In[]:
# Delta G
%matplotlib widget

# Set the parameters for the plots so they all look the same
mpl.rcParams.update({
    'font.size': 10,
    'axes.labelsize': 10,
    'axes.titlesize': 10,
    'legend.fontsize': 10,
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,
    'figure.dpi': 150,
    'grid.alpha': 0.3,
    'axes.spines.top': True,
    'axes.spines.right': True,
    'font.family': 'Serif'
})

#In[]:
# Plot heatmap


# These functions will operate as a model of the VCO
def fosc_interp_Hz(x_V):
    interp = interp1d(Vin_V, fosc_tt_Hz, kind="linear",
                  bounds_error=False,
                  fill_value=(0, fosc_tt_Hz[-1]))
    return interp(x_V)

def Vin_interp_V(x_Hz):
    interp = interp1d(fosc_tt_Hz, Vin_V, kind="linear",
                  bounds_error=False,
                  fill_value=(np.nan, Vin_V[-1]))
    return interp(x_Hz)

# Compute the sensitivity as a function of the base conductance, sampling frequency and applied current
def dg_S(g_S, fs_sps, i_A ):
    Vdd_V   = 0.8
    vin_V   = (0.8 - i_A/g_S)
    fosc_tt_Hz = fosc_interp_Hz(vin_V)
    dVin_V  = Vin_interp_V( (fosc_tt_Hz**2)/(fosc_tt_Hz - fs_sps) )
    reldVin_V = (dVin_V-Vdd_V)
    dg_S    = (reldVin_V*g_S + i_A)/reldVin_V
    return abs(dg_S)

# def dg_S(g_S, fs_sps, i_A ):
#     Vdd_V   = 0.8
#     vin_V   = (0.8 - i_A/g_S)
#     fosc_tt_Hz = fosc_interp_Hz(vin_V)
#     dVin_V  = Vin_interp_V( (fosc_tt_Hz**2)/(fosc_tt_Hz - fs_sps) )
#     dg_S    = (-dVin_V*g_S + Vdd_V*g_S + i_A)/(-Vdd_V + dVin_V)
#     return dg_S

# Set some default values in order to show "cuts" of the space.
# This will serve as an operational point (OP)
# These are the only values with which you should be playing
op_S     = 20e-6        # The conductance OP in Siemens
op_A     = iDAC_LSB_A*7     # The iDAC's output current OP in Ampere
op_sps   = 2             # The ADc's sampling frequency, in samples per second

# Set the range of the variables. This is taken from literature.
g_S_vals    = np.linspace(1e-6, 100e-6, 100)
i_A_vals    = np.arange(0, 256) * 40e-9
fs_Hz_vals  = np.linspace(1,10e3,1000)

# Name of the plots
I_VS_G = 0
F_VS_G = 1
F_VS_I = 2

# Force a minimum and maximum range of sensitivity
dg_min = np.nanmin([np.nanmin(dg_vals_S) for dg_vals_S in [
    abs(dg_S(np.meshgrid(g_S_vals, i_A_vals)[0], op_sps, np.meshgrid(g_S_vals, i_A_vals)[1])),
    abs(dg_S(np.meshgrid(g_S_vals, fs_Hz_vals)[0], np.meshgrid(g_S_vals, fs_Hz_vals)[1], op_A)),
    abs(dg_S(op_S, np.meshgrid(i_A_vals, fs_Hz_vals)[1], np.meshgrid(i_A_vals, fs_Hz_vals)[0]))
]])
dg_max = 100e-6  # since you clip above this


fig, axs = plt.subplots(1,3, figsize=(10,3), width_ratios=[0.31,0.31,0.38])

for case in range(3):
# There are three types of plots we want, for the 3-D space of variables i, G and fs
# I_VS_G: plot iDACs current vs conductance, leaving sampling frequency constant
# F_VS_G: You got the idea...
# F_VS_I: idem
    if case == I_VS_G:
        xs = g_S_vals
        ys = i_A_vals
        G, I = np.meshgrid(xs, ys)
        dg_vals_S = abs(dg_S(G, op_sps, I))
    if case == F_VS_G:
        xs = g_S_vals
        ys = fs_Hz_vals
        G, F = np.meshgrid(xs, ys)
        dg_vals_S = abs(dg_S(G, F, op_A))
    if case == F_VS_I:
        xs = i_A_vals
        ys = fs_Hz_vals
        I, F = np.meshgrid(xs, ys)
        dg_vals_S = abs(dg_S(op_S, F, I))

    dg_vals_S[ dg_vals_S > 100e-6] = np.nan

    # plot heatmap
    im = axs[case].imshow(dg_vals_S, aspect='auto', origin='lower',
                          cmap='jet_r', extent=[xs.min(), xs.max(), ys.min(), ys.max()],
                          norm=LogNorm(vmin=dg_min, vmax=dg_max))

    if case == I_VS_G:
        axs[case].set_xlabel(r"$G\,\mathrm{(S)}$")
        axs[case].set_xscale('log')
        axs[case].set_ylabel(r"$i_{DC}\,\mathrm{(A)}$")
        axs[case].set_title(f"for fs={op_sps:1.0f}Hz, ")
        axs[case].axhline(op_A, linestyle='--', color='k', linewidth=1)
        axs[case].axvline(op_S, linestyle='--', color='k', linewidth=1)
        x0, y0 = op_S, op_A
    if case == F_VS_G:
        axs[case].set_xlabel(r"$G\,\mathrm{(S)}$")
        axs[case].set_xscale('log')
        axs[case].set_ylabel(r"$f_{s}\,\mathrm{(Hz)}$")
        axs[case].set_yscale('log')
        axs[case].set_title(f"for i={op_A*1e6:1.2f}µA, ")
        axs[case].axvline(op_S, linestyle='--', color='k', linewidth=1)
        axs[case].axhline(op_sps, linestyle='--', color='k', linewidth=1)
        x0, y0 = op_S, op_sps
    if case == F_VS_I:
        axs[case].set_xlabel(r"$i_{DC}\,\mathrm{(A)}$")
        axs[case].set_ylabel(r"$f_{s}\,\mathrm{(Hz)}$")
        axs[case].set_yscale('log')
        axs[case].set_title(f"for G={op_S*1e6:1.0f}µS, ")
        axs[case].axvline(op_A, linestyle='--', color='k', linewidth=1)
        axs[case].axhline(op_sps, linestyle='--', color='k', linewidth=1)
        x0, y0 = op_A, op_sps

    # Compute the operational point of sensitivity obtained (in difference of Siemens - dS)
    ix = np.argmin(np.abs(xs - x0))
    iy = np.argmin(np.abs(ys - y0))
    op_dS = dg_vals_S[iy, ix]
    axs[case].scatter(x0, y0, color='k',s=10)

    # Format the values on the axis
    axs[case].xaxis.set_major_formatter(ScalarFormatter(useMathText=True))
    axs[case].yaxis.set_major_formatter(ScalarFormatter(useMathText=True))
    axs[case].ticklabel_format(style='sci', axis='both', scilimits=(0,0))

fig.colorbar(im,  label=r"$\Delta G$ (S)")

# Compute the operational points that were not selected
op_V    = (0.8-op_A/op_S)      # The input voltage to the ADC, in Volts
op_O    = (1/op_S)             # The resistance of the tissue, in Ohms
op_Hz   = fosc_interp_Hz(op_V)  # The VCO's oscillation frequency, in Hertz

fig.suptitle(f"ΔG={op_dS*1e9:1.1f} nS | R={op_O/1e3:1.0f} kΩ | Vin={op_V*1e3:1.0f} mV | fosc={op_Hz/1e3:1.0f} kHz")
plt.tight_layout()
plt.savefig("./out/heatmap_sensitivity.png", transparent=True)
plt.show()

'''
Notes on this:

It is counter-intuitive that for a larger conductance (e.g. towards the right of the sensitivity decreases).
This is because we are looking measuring resistance.
The same difference in conductance at a low conductance level will have a much larger effect on resistance,
which will translate into a larger effect in the voltage difference.
This means that the 1/x relationship between R and G dominates sensitivity, and not the f/V transfer function.
Example. the VCO oscillates from 30kHz to 800kHz (26x increase), at a fixed current of 1uA,
that occurs if the resistance varies from 400kOhm to  1kOhm, which makes the conductance vary from 2.5uS to 1 mS (400x decrease),
the VCO does not have enough gain to keep the relative sensitivity constant.
'''



#In[]:
# Get power estimation
# To get a power estimation we build a power model of the ADC (VCO + counter) and the DAC.
plt.figure(figsize=(6,3))

# The VCO power is taken from a variable already computed
with open("./VCO/in/P_vco_vs_Vin.pkl",'rb') as f:
    Vin_mV, Pvco_W = pickle.load(f)
Vin_p_V = Vin_mV/1e3    # Units are converted to standard units

plt.plot(fosc_interp_Hz(Vin_p_V)/1e3, Pvco_W*1e6, linestyle=':', color='gray', label='VCO')


# The VCO power is taken from a variable already computed
with open("./VCO/in/P_cnt_vs_Vin.pkl",'rb') as f:
    Vin_mV, Pcnt_W = pickle.load(f)
Vin_p_V = Vin_mV/1e3    # Units are converted to standard units

plt.plot(fosc_interp_Hz(Vin_p_V)/1e3, Pcnt_W*1e6, linestyle='--', color='gray', label='Counter')

# The VCO power is taken from a variable already computed
with open("./VCO/in/P_total_vs_Vin.pkl",'rb') as f:
    Vin_mV, Pt_W = pickle.load(f)
Vin_p_V = Vin_mV/1e3    # Units are converted to standard units
plt.plot(fosc_interp_Hz(Vin_p_V)/1e3, Pt_W*1e6, linestyle='-', color='black', label='Total')

plt.ylabel("Power (µW)")
plt.xlabel("Oscillation Frequency (kHz)")
plt.grid(which='both')
plt.legend(loc='upper left')
plt.xlim(-100,1100)
plt.tight_layout()
plt.savefig("./out/P_vco_cnt.png")
plt.show()




#In[]:
# Show the Power trade off between iDAC and ADC

# An interpolator to be able to take any value
def Padc_total_interp_W(x_V):
    interp = interp1d(Vin_p_V, Pt_W, kind="linear",
                  bounds_error=False,
                  fill_value=(0, Pt_W[-1]))
    return interp(x_V)

# The iDAC's power model is fai
def Pdac( x_A, x_V ):
    base_W  = (400e-9)*(300e-3)     # Reference current x Vds of the transistors
    out_W   = x_A*x_V               # The output current x the voltage at that point
    return (out_W + base_W)         # Total power of the iDAC

# Just for simplicity define the Resistance as the inverse of the conductivity
r_O_vals    = 1/g_S_vals

cmap = cm.get_cmap("jet")
norm = colors.Normalize(vmin=min(i_A_vals), vmax=max(i_A_vals))

fig, axs = plt.subplots(1,2, figsize=(8,3),width_ratios=[1,1], sharey=True)

# For each current value (skipping some for clarity) plot the total power as a function of the conductivity
for iout_A in i_A_vals[::32]:
    # Compute the voltage at the input of the iDAC/VCO
    x_V    = Vdd_V - r_O_vals*iout_A
    # The power is the sum of both powers
    power_total = Padc_total_interp_W(x_V) + Pdac(iout_A, x_V)

    axs[1].plot(g_S_vals*1e6, power_total*1e6, color=cmap(norm(iout_A)), label=f'{iout_A*1e6:1.2f} µA')
# Additionally plot the selected OP
axs[1].plot(g_S_vals*1e6, (Padc_total_interp_W(Vdd_V - r_O_vals*op_A) + Pdac(op_A,x_V))*1e6, color=cmap(norm(op_A)), linestyle='-.', alpha=0.5)

# Plot the breakdown between iDAC and ADC power for comparison, at the OP
axs[0].plot(g_S_vals*1e6, Padc_total_interp_W(Vdd_V - r_O_vals*op_A)*1e6, color=cmap(norm(op_A)), linestyle=':', label='VCO power')
axs[0].plot(g_S_vals*1e6, Pdac(op_A,Vdd_V - r_O_vals*op_A )*1e6, color=cmap(norm(op_A)), linestyle='--', label="iDAC power")

# Plot the OP
op_W = (Padc_total_interp_W(op_V) + Pdac(op_A, op_V))
axs[1].axvline(op_S*1e6, linestyle='--', color='k', linewidth=1)
axs[1].axhline(op_W*1e6, linestyle='--', color='k', linewidth=1)
axs[1].scatter(op_S*1e6, op_W*1e6, s=20, marker='o', color='k')

axs[0].set_ylim(0,25)
axs[0].set_ylabel("Power(µW)")
axs[0].set_title("Power iDAC & ADC at OP")
axs[1].set_title(f"Power iDAC + ADC: {op_W*1e6:1.1f} µW")
axs[0].set_xlabel("G (µS)")
axs[1].set_xlabel("G (µS)")

axs[0].legend(loc="upper left", frameon=False)
axs[1].legend(loc="center left", bbox_to_anchor=(1, 0.5), frameon=False, title=r"iDAC's $i_\text{out}$")


plt.tight_layout()
plt.savefig("./out/Ptotal_vs_G_vs_i.png", transparent=True)
plt.show()


#In[]
# Plot Pareto curves

g_S_vals    = np.linspace(1e-6, 100e-6, 10)
fs_Hz_vals  = [1, 2, 4, 8, 16]
i_A_vals    = np.linspace(0, 256,10) * 40e-9

powers          = []
sensitivities   = []
currents        = []
conductances    = []
frequencies     = []

iss = i_A_vals
gss = g_S_vals
fss = fs_Hz_vals

for i in iss:
    for g in gss:
        for f in fss:
            sensitivity = dg_S(g, f, i)
            # Compute the voltage at the input of the iDAC/VCO
            v           = Vdd_V - (1/g)*i
            # The power is the sum of both powers
            power_total = Padc_total_interp_W(v) + Pdac(i, v)

            sensitivities.  append(sensitivity)
            powers.         append(power_total)
            currents.       append(i)
            conductances.   append(g)
            frequencies.    append(f)

#In[]:
#Plot results

from matplotlib.colors import LogNorm, Normalize

mask            = (np.array(powers) > 2e-6) & (np.array(sensitivities) < 1)
sensitivities   = np.array(sensitivities)[mask]
conductances    = np.array(conductances)[mask]
powers          = np.array(powers)[mask]
frequencies     = np.array(frequencies)[mask]
currents        = np.array(currents)[mask]

order = np.argsort(powers)
powers_sorted = powers[order]
sensitivities_sorted = sensitivities[order]
conductances_sorted = conductances[order]
frequencies_sorted = frequencies[order]

fig, axs = plt.subplots(1,3, figsize=(6,2), sharey=True)
for ax, c, t in zip(axs, [conductances*1e6, currents*1e6, frequencies], ["Conductance (µS)", "Current (µA)", r"$f_s$ (Hz)" ]):
    n = Normalize() #
    ax.set_title(t)
    ax.set_xlabel("Power (µW)")
    ax.scatter(powers*1e6, sensitivities*1e6, c=c, cmap="viridis", norm=n, alpha=1, s=1e-1) #s=5*(s/max(s))**3
axs[0].set_yscale('log')
axs[0].set_ylim(1e-5,1e-1)
axs[0].set_ylabel("Sensitivity (S)")
plt.show()

#In[]:

import numpy as np
import plotly.graph_objects as go
from scipy.interpolate import griddata

x = powers*1e6
y = np.log(sensitivities)
z = conductances*1e6
c = currents*1e6

fig = go.Figure()

cmaps = ["Blues_r", "Greens_r", "YlOrBr_r", "Oranges_r", "Reds_r" ]

for i, f in enumerate(fss):
    mask = frequencies == f
    xm, ym, zm, cms = x[mask], y[mask], z[mask], c[mask]
    if xm.size < 3:
        continue

    xi = np.linspace(xm.min(), xm.max(), 10)
    yi = np.linspace(ym.min(), ym.max(), 10)
    XX, YY = np.meshgrid(xi, yi)
    ZZ = griddata((xm, ym), zm, (XX, YY), method="linear")
    CC = griddata((xm, ym), cms, (XX, YY), method="linear")

    fig.add_surface(
        x=XX, y=YY, z=ZZ,
        surfacecolor=CC,
        colorscale=cmaps[i % len(cmaps)],
        showscale=False,
        opacity=1,
        lighting=dict(ambient=1, diffuse=0, specular=0, fresnel=0, roughness=1)
    )

fig.update_layout(
    title = f"Color: frequency (cooler is lower\nIntensity: current (white is higher)",
    scene=dict(
        xaxis_title="Power (µW)",
        yaxis_title="log(sensitivity (µS))",
        zaxis_title="Conductance (µS)",
    ),
    width   = 700,
    height  = 700
)

fig.show()
