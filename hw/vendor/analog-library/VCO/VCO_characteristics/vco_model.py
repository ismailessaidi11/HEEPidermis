import pandas as pd
import numpy as np
import os
from scipy.interpolate import interp1d, RegularGridInterpolator

class VCOADCModel:
    def __init__(self, data_folder='data'):
        self.data_folder = data_folder
        self.interp_freq = None
        self.interp_pvco = None
        self.interp_pcnt = None
        self.interp_adev = None
        self.kvco_func = None
        self.v_range = None
        self.load_and_train()

    def _clean_csv(self, filename):
        path = os.path.join(self.data_folder, filename)
        df = pd.read_csv(path)
        for col in df.columns:
            df[col] = pd.to_numeric(df[col].astype(str).str.replace(',', ''), errors='coerce')
        return df

    def _build_adev_grid(self, df_n, fs_orig=10):
        voltages = np.array([float(c) for c in df_n.columns])
        taus = np.logspace(np.log10(0.1), np.log10(5.0), 20)
        grid = np.zeros((len(voltages), len(taus)))

        for i, col in enumerate(df_n.columns):
            data = df_n[col].dropna().values
            y = data / np.mean(data)
            for j, tau in enumerate(taus):
                m = int(round(tau * fs_orig))
                if 2 * m > len(y):
                    grid[i, j] = np.nan
                    continue
                weights = np.ones(m) / m
                y_bar = np.convolve(y, weights, 'valid')
                grid[i, j] = np.sqrt(0.5 * np.mean((y_bar[m:] - y_bar[:-m])**2))
        return voltages, taus, grid

    def load_and_train(self):
        """Processes CSVs and refreshes interpolators."""
        # Transfer and Power
        df_t = self._clean_csv('VCO variability - transfer.csv')
        v_t = df_t.iloc[:, 0].values / 1000.0  # mV to V
        f_t = df_t.iloc[:, 1].values * 1000.0  # kHz to Hz

        self.v_range = (v_t.min(), v_t.max())
        self.interp_freq = interp1d(v_t, f_t, kind='linear', fill_value="extrapolate")
        self.interp_pvco = interp1d(v_t, df_t.iloc[:, 2].values, kind='linear', fill_value="extrapolate")
        self.interp_pcnt = interp1d(v_t, df_t.iloc[:, 3].values, kind='linear', fill_value="extrapolate")
        self.kvco_func = lambda v: (self.interp_freq(v + 1e-5) - self.interp_freq(v - 1e-5)) / 2e-5

        # Variability (ADEV)
        df_n = self._clean_csv('VCO variability - summary N.csv')
        v_n, taus, grid = self._build_adev_grid(df_n)
        self.interp_adev = RegularGridInterpolator((v_n, taus), grid, bounds_error=False, fill_value=None)

    def predict(self, vin_v, fs_adc=10, mode='single', v_cm=0.5):
        """
        vin_v: Input voltage (V). In 'diff' mode, this is the differential voltage (Vp-Vn).
        mode: 'single' or 'diff' (pseudo-differential).
        v_cm: Common-mode voltage for differential mode.
        """
        vin_v = np.atleast_1d(vin_v)
        fs_adc = np.atleast_1d(fs_adc)
        tau = np.clip(1.0 / fs_adc, 0.1, 5.0)

        # Handle broadcasting if fs_adc and vin_v have different shapes
        # e.g., if vin is 3D and fs is a scalar
        vin_v, tau = np.broadcast_arrays(vin_v, tau)

        if mode == 'single':
            f = self.interp_freq(vin_v)
            k = self.kvco_func(vin_v)

            # Vectorized 2D lookup: needs an (N, 2) array of points
            query_pts = np.stack([vin_v.ravel(), tau.ravel()], axis=-1)
            adev = self.interp_adev(query_pts).reshape(vin_v.shape)
            f_osc_error_Hz = f*adev
            ire_V = f_osc_error_Hz / np.abs(k)
            # Power (vectorized)
            pvco = self.interp_pvco(vin_v)
            pcnt = self.interp_pcnt(vin_v)
            p_tot = pvco + pcnt
            return {"f_osc": f, "k":k, "p_vco": pvco, "p_cnt": pcnt, "p_tot": p_tot, "p_vco2": 0, "p_cnt2": 0,"p_tot2": 0, "f_osc_error":f_osc_error_Hz,"ire": ire_V}
        else:
            # ... [Differential logic vectorized similarly] ...
            vp, vn = v_cm + vin_v/2, v_cm - vin_v/2
            f = self.interp_freq(vp) - self.interp_freq(vn)
            k = 0.5 * (self.kvco_func(vp) + self.kvco_func(vn))
            # ADEV lookups for both P and N branches
            pts_p = np.stack([vp.ravel(), tau.ravel()], axis=-1)
            pts_n = np.stack([vn.ravel(), tau.ravel()], axis=-1)
            adev_p = self.interp_adev(pts_p).reshape(vin_v.shape)
            adev_n = self.interp_adev(pts_n).reshape(vin_v.shape)
            ire_V = np.sqrt((adev_p*self.interp_freq(vp))**2 + (adev_n*self.interp_freq(vn))**2) / (2*np.abs(k))
            pvco = self.interp_pvco(vp)
            pcnt = self.interp_pcnt(vp)
            p_tot = pvco + pcnt
            pvco2 = self.interp_pvco(vn)
            pcnt2 = self.interp_pcnt(vn)
            ptot2 = pvco + pcnt + pvco2 + pcnt2
            return {"f_osc": f, "k":k, "p_vco": pvco, "p_cnt": pcnt, "p_tot": p_tot, "p_vco2": pvco2, "p_cnt2": pcnt2,"p_tot2": ptot2, "ire": ire_V}
