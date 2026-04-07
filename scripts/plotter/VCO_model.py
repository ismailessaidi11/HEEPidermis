from dataclasses import dataclass

import numpy as np
import pandas as pd
from scipy.optimize import curve_fit
from scipy.interpolate import RegularGridInterpolator
import os


# ---------------------------------------------------------------------------
# Data Models and Computation Classes
# ---------------------------------------------------------------------------
@dataclass
class VCOParams:
    vdd: float = 0.8

    vin_min_mV: float = 330.0
    vin_max_mV: float = 800.0
    vin_points: int = 300

    idc_min: float = 0.1
    idc_max: float = 10.0
    idc_points: int = 300

    sweep_points: int = 200

    allen_dev: bool = True

    @property
    def vin_range(self):
        return np.linspace(self.vin_min_mV, self.vin_max_mV, self.vin_points)

    @property
    def i_dc_range(self):
        return np.linspace(self.idc_min, self.idc_max, self.idc_points)

class VCO_Model:
    def __init__(self, data_folder, params=None, representation="poly"):
        self.params = params if params is not None else VCOParams()
        self.representation = representation.lower()
        
        VCO_data_path = os.path.join(data_folder, "VCO_data.csv")
        df = pd.read_csv(VCO_data_path)

        self.vin_data = np.asarray(df.Vin, dtype=float)
        self.fosc_data = np.asarray(df.fosc, dtype=float)
        self.pvco_data = np.asarray(df.P_VCO, dtype=float)
        self.pcnt_data = np.asarray(df.P_counter, dtype=float)
        self.vdd = float(self.params.vdd)

        nonzero_indices = np.where(self.fosc_data > 0)[0]
        if len(nonzero_indices) == 0:
            raise ValueError("fosc_data contains no positive values; cannot determine threshold.")

        self.piecewise_threshold = float(self.vin_data[nonzero_indices[0]])

        mask = self.vin_data >= self.piecewise_threshold
        self.vin_active = self.vin_data[mask]
        self.fosc_active = self.fosc_data[mask]
        if np.any(np.diff(self.fosc_active) <= 0):
            raise ValueError(
                "LUT inversion requires fosc_active to be strictly increasing."
            )
        # Precompute the derivative LUT for dV/dF to speed up delta_G calculations
        self.df_dv_lut = self._build_lut_derivative(self.vin_active, self.fosc_active)

        self.interp_adev = None
        if self.params.allen_dev:
            adev_path = os.path.join(data_folder, "VCO variability - summary N.csv")
            df_n = self._clean_csv_generic(adev_path)
            v_n, taus, grid = self._build_adev_grid(df_n)
            self.interp_adev = RegularGridInterpolator(
                (v_n, taus), grid, bounds_error=False, fill_value=None
            )
        self.popt_poly, _ = curve_fit(self.polynomial_model, self.vin_active, self.fosc_active)

    @staticmethod
    def polynomial_model(x, a, b, c):
        return a * x**2 + b * x + c

    def _build_lut_derivative(self, x, y):
        x = np.asarray(x, dtype=float)
        y = np.asarray(y, dtype=float)

        dydx = np.empty_like(y, dtype=float)

        if len(x) < 2:
            raise ValueError("Need at least two LUT points to compute derivative.")

        # One-sided differences at boundaries
        dydx[0] = (y[1] - y[0]) / (x[1] - x[0])
        dydx[-1] = (y[-1] - y[-2]) / (x[-1] - x[-2])

        # Central differences inside
        dydx[1:-1] = (y[2:] - y[:-2]) / (x[2:] - x[:-2])

        return dydx
    
    def _clean_csv_generic(self, path):
        df = pd.read_csv(path)
        for col in df.columns:
            df[col] = pd.to_numeric(df[col].astype(str).str.replace(',', ''), errors='coerce')
        return df
    
    def _build_adev_grid(self, df_n, fs_orig=10):
        voltages = np.array([float(c) for c in df_n.columns], dtype=float)  # expected in V
        taus = np.logspace(np.log10(0.1), np.log10(5.0), 20)
        grid = np.zeros((len(voltages), len(taus)))

        for i, col in enumerate(df_n.columns):
            data = df_n[col].dropna().values
            y = data / np.mean(data)  # fractional frequency

            for j, tau in enumerate(taus):
                m = int(round(tau * fs_orig))
                if 2 * m > len(y):
                    grid[i, j] = np.nan
                    continue

                weights = np.ones(m) / m
                y_bar = np.convolve(y, weights, 'valid')
                grid[i, j] = np.sqrt(0.5 * np.mean((y_bar[m:] - y_bar[:-m])**2))

        return voltages, taus, grid
    
    def _prepare_grid(self, vin_mV, i_dc_uA):
        vin_arr = np.atleast_1d(np.asarray(vin_mV, dtype=float))
        i_arr = np.atleast_1d(np.asarray(i_dc_uA, dtype=float))

        vin_2d = vin_arr[None, :]
        i_2d = i_arr[:, None]

        vin_scalar = np.isscalar(vin_mV)
        idc_scalar = np.isscalar(i_dc_uA)

        return vin_arr, i_arr, vin_2d, i_2d, vin_scalar, idc_scalar

    def _vin_idc_terms(self, vin_2d, i_2d):
        vin_V = vin_2d * 1e-3
        i_dc_A = i_2d * 1e-6
        denom = self.vdd - vin_V
        return vin_V, i_dc_A, denom

    def kvco_kHz_per_mV(self, vin_mV):
        vin_arr = np.asarray(vin_mV, dtype=float)

        if self.representation == "poly":
            a, b, _ = self.popt_poly
            out = np.where(
                vin_arr < self.piecewise_threshold,
                0.0,
                2 * a * vin_arr + b
            )

        elif self.representation == "lut":
            out = np.interp(
                vin_arr,
                self.vin_active,
                self.df_dv_lut,
                left=0.0,
                right=self.df_dv_lut[-1]
            )
            out = np.where(vin_arr < self.piecewise_threshold, 0.0, out)

        else:
            raise ValueError(f"Unknown representation: {self.representation}")

        return out.item() if np.isscalar(vin_mV) else out

    def _invalid_mask(self, vin_2d, denom, extra_mask=None):
        invalid = (
            (vin_2d < self.piecewise_threshold) |
            np.isnan(vin_2d) |
            (denom <= 0)
        )
        if extra_mask is not None:
            invalid = invalid | extra_mask
        return invalid

    def _restore_shape(self, out, vin_scalar, idc_scalar):
        if vin_scalar and idc_scalar:
            return out[0, 0].item()
        elif vin_scalar:
            return out[:, 0]
        elif idc_scalar:
            return out[0, :]
        return out

    def _evaluate_over_vin_sweep(self, evaluator, vin_min=None, **kwargs):
        if vin_min is None:
            vin_min = self.piecewise_threshold

        vin_smooth = np.linspace(vin_min, self.params.vin_max, self.params.sweep_points)
        values = evaluator(vin_smooth, self.params.i_dc_range, **kwargs)
        return vin_smooth, values
    
    def fosc_from_vin(self, vin_mV):
        """
        Piecewise polynomial model:
            if representation is "poly":
                f_osc = 0 if V_in < piecewise_threshold, else polynomial 
            else: LUT interpolation if representation is "lut".
        """
        vin_arr = np.asarray(vin_mV, dtype=float)

        if self.representation == "poly":
            a, b, c = self.popt_poly
            poly_val = np.maximum(a * vin_arr**2 + b * vin_arr + c, 0.0)
            out = np.where(vin_arr < self.piecewise_threshold, 0.0, poly_val)

        elif self.representation == "lut":
            out = np.interp(
                vin_arr,
                self.vin_active,
                self.fosc_active,
                left=0.0,
                right=self.fosc_active[-1]
            )
            out = np.where(vin_arr < self.piecewise_threshold, 0.0, out)

        else:
            raise ValueError(f"Unknown representation: {self.representation}")

        return out.item() if np.isscalar(vin_mV) else out

    def vin_from_fosc(self, fosc_kHz):
        fosc_arr = np.asarray(fosc_kHz, dtype=float)

        if self.representation == "poly":
            a, b, c = self.popt_poly
            out = np.full_like(fosc_arr, np.nan, dtype=float)

            zero_mask = fosc_arr <= 0
            out[zero_mask] = self.piecewise_threshold

            pos_mask = fosc_arr > 0
            if np.any(pos_mask):
                fpos = fosc_arr[pos_mask]
                disc = b**2 - 4 * a * (c - fpos)

                valid_disc = disc >= 0
                v_candidates = np.full_like(fpos, np.nan, dtype=float)

                sqrt_disc = np.sqrt(np.maximum(disc, 0.0))
                v1 = (-b + sqrt_disc) / (2 * a)
                v2 = (-b - sqrt_disc) / (2 * a)

                cand1_ok = (v1 >= self.piecewise_threshold) & valid_disc
                cand2_ok = (v2 >= self.piecewise_threshold) & valid_disc

                v_candidates[cand1_ok] = v1[cand1_ok]

                replace_mask = np.isnan(v_candidates) & cand2_ok
                v_candidates[replace_mask] = v2[replace_mask]

                out[pos_mask] = v_candidates

        elif self.representation == "lut":
            out = np.full_like(fosc_arr, np.nan, dtype=float)

            zero_mask = fosc_arr <= 0
            out[zero_mask] = self.piecewise_threshold

            pos_mask = fosc_arr > 0
            if np.any(pos_mask):
                fmin = self.fosc_active[0]
                fmax = self.fosc_active[-1]

                fpos = fosc_arr[pos_mask]
                valid = (fpos >= fmin) & (fpos <= fmax)

                vin_vals = np.full_like(fpos, np.nan, dtype=float)
                vin_vals[valid] = np.interp(
                    fpos[valid],
                    self.fosc_active,
                    self.vin_active
                )

                out[pos_mask] = vin_vals

        else:
            raise ValueError(f"Unknown representation: {self.representation}")

        return out.item() if np.isscalar(fosc_kHz) else out
    
    def vin_from_G(self, G_uS, i_dc_uA):
        G_arr = np.asarray(G_uS, dtype=float) * 1e-6
        i_arr = np.asarray(i_dc_uA, dtype=float) * 1e-6

        with np.errstate(divide='ignore', invalid='ignore'):
            vin_V = self.vdd - i_arr / G_arr

        vin_mV = vin_V * 1e3
        invalid = (G_arr <= 0) | np.isnan(G_arr) | np.isnan(i_arr)
        vin_mV = np.where(invalid, np.nan, vin_mV)

        return vin_mV.item() if np.ndim(vin_mV) == 0 else vin_mV

    def vdd_mV(self):
        return self.vdd * 1e3

    def conductance(self, vin_mV, i_dc_uA):
        _, _, vin_2d, i_2d, vin_scalar, idc_scalar = self._prepare_grid(vin_mV, i_dc_uA)
        _, i_dc_A, denom = self._vin_idc_terms(vin_2d, i_2d)

        with np.errstate(divide='ignore', invalid='ignore'):
            G_uS = (i_dc_A / denom) * 1e6

        invalid = self._invalid_mask(vin_2d, denom) | np.isnan(i_2d)
        G_uS = np.where(invalid, np.nan, G_uS)

        return self._restore_shape(G_uS, vin_scalar, idc_scalar)

    def df_osc_adev_Hz(self, vin_mV, fs_Hz):
        if self.interp_adev is None:
            fs_arr = np.asarray(fs_Hz, dtype=float)
            return np.zeros_like(fs_arr, dtype=float)

        vin_arr = np.asarray(vin_mV, dtype=float)
        fs_arr = np.asarray(fs_Hz, dtype=float)

        vin_V = vin_arr * 1e-3
        tau = np.clip(1.0 / fs_arr, 0.1, 5.0)

        vin_V, tau = np.broadcast_arrays(vin_V, tau)

        f_osc_Hz = np.asarray(self.fosc_from_vin(vin_arr), dtype=float) * 1e3
        f_osc_Hz, _ = np.broadcast_arrays(f_osc_Hz, tau)

        query_pts = np.stack([vin_V.ravel(), tau.ravel()], axis=-1)
        adev = self.interp_adev(query_pts).reshape(vin_V.shape)

        out = f_osc_Hz * adev
        return out.item() if out.ndim == 0 else out
    
    def df_osc_sampling_Hz(self, fs_Hz=None, avg_window=1):
        if fs_Hz is None:
            raise ValueError("fs_Hz must be provided to compute delta_f_osc_Hz.")

        fs_Hz = np.asarray(fs_Hz, dtype=float)
        avg_window = float(avg_window)
        invalid = (fs_Hz <= 0) | (avg_window <= 0)
        result = np.where(invalid, np.nan, fs_Hz * np.sqrt(avg_window))

        return result.item() if np.ndim(result) == 0 else result

    def df_osc_Hz(self, vin_mV, fs_Hz, variance=1, avg_window=1):
        df_samp = self.df_osc_sampling_Hz(fs_Hz, avg_window=avg_window)
        df_adev = variance * self.df_osc_adev_Hz(vin_mV, fs_Hz)
        out = np.maximum(df_samp, df_adev)
        return out.item() if np.ndim(out) == 0 else out
    
    def dVin_mV(self, df_osc_Hz, vin_mV):
        k_kHz_per_mV = self.kvco_kHz_per_mV(vin_mV)

        # Convert k from kHz/mV to Hz/mV
        k_Hz_per_mV = np.asarray(k_kHz_per_mV, dtype=float) * 1e3
        df_osc_Hz = np.asarray(df_osc_Hz, dtype=float)

        with np.errstate(divide='ignore', invalid='ignore'):
            out = df_osc_Hz / k_Hz_per_mV

        return out.item() if out.ndim == 0 else out

    def delta_G_uS(self, G_uS, vin_mV, i_dc_uA, fs_Hz, variance=1, avg_window=1):
        k_vco_kHz_per_mV = self.kvco_kHz_per_mV(vin_mV)  
        df_osc = self.df_osc_Hz(vin_mV=vin_mV, fs_Hz=fs_Hz, variance=variance, avg_window=avg_window)

        _, _, vin_2d, _, vin_scalar, idc_scalar = self._prepare_grid(vin_mV, i_dc_uA)
        vin_V = vin_2d * 1e-3

        G_2d = np.asarray(G_uS, dtype=float)
        if G_2d.ndim == 0:
            G_2d = np.array([[G_2d]])
        elif vin_scalar:
            G_2d = G_2d[:, None]
        elif idc_scalar:
            G_2d = G_2d[None, :]

        k_vco_kHz_per_mV_2d = np.asarray(k_vco_kHz_per_mV, dtype=float)
        if k_vco_kHz_per_mV_2d.ndim == 0:
            k_vco_kHz_per_mV_2d = np.full_like(vin_2d, k_vco_kHz_per_mV_2d, dtype=float)
        else:
            k_vco_kHz_per_mV_2d = np.broadcast_to(k_vco_kHz_per_mV_2d, k_vco_kHz_per_mV_2d.shape)

        k_vco_Hz_per_V = k_vco_kHz_per_mV_2d * 1e6

        with np.errstate(divide='ignore', invalid='ignore'):
            dG_uS = np.abs((df_osc * G_2d**2) / (k_vco_Hz_per_V * i_dc_uA + df_osc * G_2d))

        invalid = self._invalid_mask(vin_2d, self.vdd - vin_V)
        dG_uS = np.where(invalid, np.nan, dG_uS)

        return self._restore_shape(dG_uS, vin_scalar, idc_scalar)

    def delta_G_curve(self, fs_Hz=None, variance=1, avg_window=1, vin_min=None):
        return self._evaluate_over_vin_sweep(
            self.delta_G_uS,
            vin_min=vin_min,
            fs_Hz=fs_Hz,
            variance=variance,
            avg_window=avg_window
        )
    
    def idc_power_uW(self, vin_mV, i_dc_uA):
        vin_arr = np.asarray(vin_mV, dtype=float)
        i_arr = np.asarray(i_dc_uA, dtype=float)

        vin_V = vin_arr * 1e-3
        i_dc_A = i_arr * 1e-6
        power_idc_uW = i_dc_A * vin_V * 1e6


        invalid = (vin_arr < self.piecewise_threshold) | np.isnan(vin_arr) | np.isnan(i_arr)
        power_idc_uW = np.where(invalid, np.nan, power_idc_uW)

        return power_idc_uW.item() if np.isscalar(vin_mV) and np.isscalar(i_dc_uA) else power_idc_uW
    
    def pvco_from_vin(self, vin_mV):
        return np.interp(vin_mV, self.vin_data, self.pvco_data)

    def pcnt_from_vin(self, vin_mV):
        return np.interp(vin_mV, self.vin_data, self.pcnt_data)
    
    def i_dc_max(self, G_uS):
        return min(float(G_uS * (self.vdd_mV() - self.params.vin_min_mV) / 1000), self.params.idc_max)
    
    def compute_delta_G_range_nS(self, G_uS, fs_Hz, variance, avg_window):
        """Compute min/max deltaG range for given G and fs values"""
        i_vals = self.params.i_dc_range
        max_i_dc = self.i_dc_max(G_uS)
        i_vals_valid = i_vals[i_vals <= max_i_dc]
        
        deltaG_vals = []
        valid_mask = []
        for i_dc in i_vals_valid:
            vin_mV = self.vin_from_G(G_uS, i_dc)
            delta_G_us = self.delta_G_uS(
                G_uS=G_uS,
                vin_mV=vin_mV,
                i_dc_uA=i_dc,
                fs_Hz=fs_Hz,
                variance=variance,
                avg_window=avg_window
            )
            deltaG_vals.append(delta_G_us)
            valid_mask.append(np.isfinite(delta_G_us))

        
        deltaG_vals_nS = np.asarray(deltaG_vals, dtype=float) * 1000  # Convert to nS
        valid_mask = np.asarray(valid_mask, dtype=bool)

        if not np.any(valid_mask):
            return np.nan, np.nan

        return np.nanmin(deltaG_vals_nS[valid_mask]), np.nanmax(deltaG_vals_nS[valid_mask])
