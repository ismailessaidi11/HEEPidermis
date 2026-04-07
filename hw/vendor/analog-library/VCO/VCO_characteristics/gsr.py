#In[]:

import scipy.io as sio
import matplotlib.pyplot as plt

'''
Data extracted from:

Miranda Calero, José Angel, et al., 2022,
"UC3M4Safety Database - WEMAC: Physiological signals",
https://doi.org/10.21950/FNUHKE, e-cienciaDatos, V3
'''


# 1. Load the .mat file
# squeeze_me=True flattens redundant dimensions (turns 1xN arrays into 1D)
# struct_as_record=False allows us to access MATLAB structs using object dot notation (.gsr instead of ['gsr'])
file_path = 'data/V038_GSR.mat'
mat_data = sio.loadmat(file_path, squeeze_me=True, struct_as_record=False)

# 2. Extract the 'out_struct' variable
# This will be a 1D array of objects because of squeeze_me=True
out_struct = mat_data['out_struct']

# 3. Access the 15th element
# MATLAB {1, 15} translates to Python index 14
recording = out_struct[14]

# 4. Navigate through the struct fields to get to 'allRaw'
# Because we used struct_as_record=False, we can use dot notation just like in MATLAB
all_raw_data = recording.GSR.allRaw

# Verify the extracted data
print("Data shape:", all_raw_data.shape)
print(all_raw_data)

plt.figure()
plt.plot(all_raw_data)
plt.show()

with open('data/gsr_pS_200Hz.txt', 'w+') as f:
    f.writelines([f"{int(d*1e6)}\n" for d in all_raw_data])


#In[]:

import numpy as np

w = 5000
mavg = [ np.average(all_raw_data[i-w:i]) for i in range(w,len(all_raw_data))]

#In[]:
%matplotlib widget

plt.figure()
plt.plot(all_raw_data)
plt.plot(mavg)
plt.show()

phasic = all_raw_data[w:]-mavg

plt.figure()
plt.plot(phasic)
plt.show()


#In[]

import pickle

with open("./data/gsr.pkl", 'wb') as f:
    pickle.dump((all_raw_data,phasic), f)