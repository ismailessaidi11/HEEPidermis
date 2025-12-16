#In[]:
# Check the results

%matplotlib widget
import numpy as np
import matplotlib.pyplot as plt


header = open("output.txt").readline().strip()
x, y = np.loadtxt("output.txt", delimiter="\t", skiprows=1, unpack=True)

plt.figure()
plt.title(header)
# plt.plot(x, y, marker="o")
plt.scatter(x, y)
plt.xlabel("x")
plt.ylabel("y")
plt.show()

#In[]:

data = np.array([v for v in np.array(y)])  # dataour data-column
MOD = 1024

data_fixed = data.copy()
for i in range(1, len(data_fixed)):
    if data_fixed[i] - data_fixed[i-1] < -MOD//2:
        data_fixed[i:] += MOD

# optional: center around 0
data_fixed -= MOD//2

plt.figure()
plt.title(header)
plt.plot(x, data_fixed, marker="o")
plt.xlabel("x")
plt.ylabel("y")
plt.show()
