#In[]:
# VCO's transfer function

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np


# Load data and define axes
df = pd.read_csv('data/VCO variability - transfer.csv')
x = df.iloc[:, 0]
y_freq, y_vco, y_cnt = df.iloc[:, 1], df.iloc[:, 2], df.iloc[:, 3]
y_tot = y_vco + y_cnt

# Setup plot with serif font
plt.rcParams['font.family'] = 'serif'
fig, ax1 = plt.subplots(figsize=(6, 2.5))

# Plot Frequency on Left Y-axis
ln1 = ax1.plot(x, y_freq, 'k-', label='Frequency')
ax1.set_xlabel('Input Voltage (mV)')
ax1.set_ylabel('Oscillation Frequency (kHz)')

# Plot Power on Right Y-axis
ax2 = ax1.twinx()
ln2 = ax2.plot(x, y_vco, color='gray', ls='--', label=r'$P_\text{VCO}$')
ln3 = ax2.plot(x, y_cnt, color='0.6', ls=':', label=r'$P_\text{counter}$')
ln4 = ax2.plot(x, y_tot, color='0.4', ls='-.', label=r'$P_\text{total}$')
ax2.set_ylabel('Power (µW)')

# Combined Legend
lns = ln1 + ln2 + ln3 + ln4
ax1.legend(lns, [l.get_label() for l in lns], loc='upper left')

plt.xlim(200, 850)
ax1.grid(visible=True,which='major',axis='x',c='lightgray',zorder=0)
plt.tight_layout()
plt.savefig('figs/vco_transfer.svg')