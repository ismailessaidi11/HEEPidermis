#In[]:

import matplotlib.pyplot as plt
import numpy as np
from vco_model import VCOADCModel

# 1. Initialize the model
vco = VCOADCModel(data_folder='data')

# 2. Generate sweep data
v_sweep = np.linspace(0.2, 0.85, 100)
single_f, single_ire = [], []
diff_f, diff_ire = [], []

for v in v_sweep:
    res_s = vco.predict(v, fs_adc=10, mode='single')
    res_d = vco.predict(v, fs_adc=10, mode='diff', v_cm=0.5)

    single_f.append(res_s['f_osc'] / 1e3) # kHz
    single_ire.append(res_s['ire'] )
    diff_f.append(res_d['f_osc'] / 1e3)
    diff_ire.append(res_d['ire'] * 1000)

# 3. Plotting
plt.rcParams['font.family'] = 'serif'
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 8))

# # Fosc vs Vin
# ax1.plot(v_sweep * 1000, single_f, 'k-', label='Single-Ended')
# ax1.plot(v_sweep * 1000, diff_f, 'r--', label='Pseudo-Diff ($V_{cm}=0.5V$)')
# ax1.set_ylabel('Oscillation Frequency (kHz)')
# ax1.set_title('Model Output: Frequency Transfer')
# ax1.grid(True, alpha=0.3)
# ax1.legend()

# IRE vs Vin
ax2.plot(v_sweep * 1000, single_ire, 'k-', label='Single-Ended')
# ax2.plot(v_sweep * 1000, diff_ire, 'r--', label='Pseudo-Diff ($V_{cm}=0.5V$)')
ax2.set_yscale('log')
ax2.set_xlabel('Input Voltage (mV)')
ax2.set_ylabel('Input Referred Error (V)')
ax2.set_title('Model Output: Voltage Uncertainty (IRE)')
ax2.grid(True, which='both', alpha=0.3)
ax2.legend()

plt.tight_layout()
plt.show()


#In[]:
# For different frequencies

# 1. Initialize the model
vco = VCOADCModel(data_folder='data')

# 2. Generate sweep data
v_sweep = np.linspace(0.2, 0.85, 100)
fss = np.logspace(-1,2,10)
ire = []
ires = []

plt.rcParams['font.family'] = 'serif'
plt.figure()

for i,fs in enumerate(fss):
    ire = []
    for v in v_sweep:
        res_s = vco.predict(v, fs_adc=fs, mode='single')
        ire.append(res_s['ire'] )
    ires.append(ire)
    plt.plot(v_sweep * 1000, ire, c='k', alpha=1/(i+1),label=f"{1/fs:1.2f} s | {fs:1.2f} Hz")



plt.yscale('log')
plt.xlabel('Input Voltage (mV)')
plt.ylabel('Input Referred Error (V)')
plt.title('Model Output: Voltage Uncertainty (IRE)')
plt.grid(True, which='both', alpha=0.3)
plt.legend()

plt.tight_layout()
plt.show()