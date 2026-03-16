from dataclasses import dataclass

import numpy as np
from scipy.optimize import curve_fit


# ---------------------------------------------------------------------------
# Data Models and Computation Classes
# ---------------------------------------------------------------------------
@dataclass
class VCOParams:
    vdd: float = 0.8

    vin_min: float = 225.0
    vin_max: float = 820.0
    vin_points: int = 300

    idc_min: float = 0.1
    idc_max: float = 10.0
    idc_points: int = 100

    @property
    def vin_range(self):
        return np.linspace(self.vin_min, self.vin_max, self.vin_points)

    @property
    def i_dc_range(self):
        return np.linspace(self.idc_min, self.idc_max, self.idc_points)

class VCO_Model:
    """
    Encapsulate f_osc ↔ V_in ↔ G relations and dG_df calculations.

    Piecewise model:
        f_osc(V_in) = 0                    if V_in < piecewise_threshold
                    = a*V_in^2 + b*V_in + c otherwise

    Notes:
    - V_in is in mV
    - f_osc is in kHz
    - i_dc is in uA
    - G is in uS
    """

    def __init__(self, vin_data, fosc_data, params=None):
        self.params = params if params is not None else VCOParams()

        self.vin_data = np.asarray(vin_data, dtype=float)
        self.fosc_data = np.asarray(fosc_data, dtype=float)

        self.vdd = float(self.params.vdd)
        self.vin_data = np.asarray(vin_data, dtype=float)
        self.fosc_data = np.asarray(fosc_data, dtype=float)
        
        nonzero_indices = np.where(self.fosc_data > 0)[0]
        if len(nonzero_indices) == 0:
            raise ValueError("fosc_data contains no positive values; cannot determine threshold.")

        # First Vin where oscillation starts
        self.piecewise_threshold = float(self.vin_data[nonzero_indices[0]])

        # Fit polynomial only in active region
        mask = self.vin_data >= self.piecewise_threshold
        vin_active = self.vin_data[mask]
        fosc_active = self.fosc_data[mask]

        self.popt_poly, _ = curve_fit(self.polynomial_model, vin_active, fosc_active)

    @staticmethod
    def polynomial_model(x, a, b, c):
        return a * x**2 + b * x + c
    

    def fosc_from_vin(self, vin_mV):
        """
        Piecewise polynomial model:
            f_osc = 0 if V_in < piecewise_threshold, else polynomial
        """
        vin_arr = np.asarray(vin_mV, dtype=float)
        a, b, c = self.popt_poly
        poly_val = np.maximum(a * vin_arr**2 + b * vin_arr + c, 0.0)
        out = np.where(vin_arr < self.piecewise_threshold, 0.0, poly_val)
        return out.item() if np.isscalar(vin_mV) else out

    def vin_from_fosc(self, fosc_kHz):
        """
        Invert the piecewise model to extract V_in from f_osc.
        For f_osc <= 0, returns the threshold as a lower bound.
        """
        fosc_arr = np.asarray(fosc_kHz, dtype=float)
        a, b, c = self.popt_poly

        out = np.full_like(fosc_arr, np.nan, dtype=float)

        # Below or at zero frequency -> below threshold / lower bound
        zero_mask = fosc_arr <= 0
        out[zero_mask] = self.piecewise_threshold

        # Invert quadratic only for positive fosc
        pos_mask = fosc_arr > 0
        if np.any(pos_mask):
            fpos = fosc_arr[pos_mask]
            disc = b**2 - 4 * a * (c - fpos)

            valid_disc = disc >= 0
            v_candidates = np.full_like(fpos, np.nan, dtype=float)

            # Two roots
            sqrt_disc = np.sqrt(np.maximum(disc, 0.0))
            v1 = (-b + sqrt_disc) / (2 * a)
            v2 = (-b - sqrt_disc) / (2 * a)

            # Keep the physically valid root above threshold
            cand1_ok = (v1 >= self.piecewise_threshold) & valid_disc
            cand2_ok = (v2 >= self.piecewise_threshold) & valid_disc

            v_candidates[cand1_ok] = v1[cand1_ok]

            replace_mask = np.isnan(v_candidates) & cand2_ok
            v_candidates[replace_mask] = v2[replace_mask]

            out[pos_mask] = v_candidates

        return out.item() if np.isscalar(fosc_kHz) else out

    def vdd_mV(self):
        return self.vdd * 1e3

    def conductance(self, vin_mV, i_dc_uA):
        """
        G in uS given V_in and i_dc.

        Uses:
            G = i_dc / (VDD - V_in)

        so vin_mV is assumed to be the sensed node voltage and the resistor
        drop is (VDD - V_in).
        """
        vin_arr = np.asarray(vin_mV, dtype=float)
        i_arr = np.asarray(i_dc_uA, dtype=float)

        vin_V = vin_arr * 1e-3
        denom = self.vdd - vin_V  # resistor voltage drop in V
        i_dc_A = i_arr * 1e-6

        # Broadcast-safe computation
        with np.errstate(divide='ignore', invalid='ignore'):
            G_uS = (i_dc_A / denom) * 1e6

        invalid = (vin_arr < self.piecewise_threshold) | np.isnan(vin_arr) | (denom <= 0)
        G_uS = np.where(invalid, np.nan, G_uS)

        return G_uS.item() if np.isscalar(vin_mV) and np.isscalar(i_dc_uA) else G_uS

    def dG_df(self, vin_mV, i_dc_uA):
        """
        G tolerance (dG/df) in units of uS/kHz.
        
        Quantifies how much G changes with f_osc measurement error.
        Higher value = greater dG_df to f_osc measurement uncertainty.

        Since:
            G = i_dc / (VDD - V_in)
        then:
            dG/dV = i_dc / (VDD - V_in)^2
        and:
            df/dV = 0 below threshold
                    2*a*V + b above threshold
        """
        vin_arr = np.asarray(vin_mV, dtype=float)
        i_arr = np.asarray(i_dc_uA, dtype=float)

        a, b, _ = self.popt_poly
        vin_V = vin_arr * 1e-3
        i_dc_A = i_arr * 1e-6

        # Piecewise df/dV: 0 below threshold, polynomial derivative above
        df_dV_mV = np.where(
            vin_arr < self.piecewise_threshold,
            0.0,
            2 * a * vin_arr + b
        )  # units: kHz/mV

        denom = self.vdd - vin_V
        with np.errstate(divide='ignore', invalid='ignore'):
            dG_dV = i_dc_A / (denom ** 2)  # S/V
            # Convert dG/dV from S/V to uS/mV:
            # (S/V) * (1e6 uS/S) * (1e-3 V/mV) = 1e3 uS/mV
            dG_dV_uS_per_mV = dG_dV * 1e3

            dG_df = dG_dV_uS_per_mV / df_dV_mV  # uS/kHz

        invalid = (
            (vin_arr < self.piecewise_threshold) |
            np.isnan(vin_arr) |
            np.isnan(i_arr) |
            (denom <= 0) |
            (df_dV_mV == 0)
        )
        dG_df = np.where(invalid, np.nan, dG_df)

        return dG_df.item() if np.isscalar(vin_mV) and np.isscalar(i_dc_uA) else dG_df

    def dG_df_curve(self, i_dc_range, vin_min=None, vin_max=None, n_points=200):
        """
        Returns G tolerance curves (dG/df) over a Vin sweep for each i_dc in i_dc_range.
        
        G tolerance quantifies the conductance uncertainty due to f_osc measurement error.
        Output shape: (len(i_dc_range), n_points)
        """
        if vin_min is None:
            vin_min = self.piecewise_threshold
        if vin_max is None:
            vin_max = max(np.max(self.vin_data), self.vdd_mV())

        vin_smooth = np.linspace(vin_min, vin_max, n_points)             
        i_dc_arr = np.asarray(i_dc_range, dtype=float)[:, None]           

        a, b, _ = self.popt_poly
        df_dV_mV = np.where(
            vin_smooth < self.piecewise_threshold,
            0.0,
            2 * a * vin_smooth + b
        )[None, :]                                                        

        vin_smooth_V = vin_smooth[None, :] * 1e-3                         
        denom = self.vdd - vin_smooth_V
        i_dc_A = i_dc_arr * 1e-6                                         

        with np.errstate(divide='ignore', invalid='ignore'):
            dG_dV = i_dc_A / (denom ** 2)                                 # S/V
            dG_dV_uS_per_mV = dG_dV * 1e3                                 # uS/mV
            sens = dG_dV_uS_per_mV / df_dV_mV                             # uS/kHz

        invalid = (vin_smooth[None, :] < self.piecewise_threshold) | (denom <= 0) | (df_dV_mV == 0)
        sens = np.where(invalid, np.nan, sens)

        return vin_smooth, sens
    
    def delta_f_kHz(self, fs_Hz=None, Ts_s=None, avg_window=1):
        """
        Estimate frequency resolution in kHz from counting over a window Ts.

        Parameters
        ----------
        fs_Hz : float or array-like, optional
            Sampling frequency in Hz.
        Ts_s : float or array-like, optional
            Measurement window in seconds.
        avg_window : int or float, optional
            Number of averaged windows. Improves resolution by sqrt(avg_window).

        Returns
        -------
        delta_f_kHz = 1 / (Ts_s * np.sqrt(avg_window)) : float or ndarray
            Estimated frequency uncertainty in kHz.
        """
        if Ts_s is None:
            if fs_Hz is None:
                return np.nan
            fs_Hz = np.asarray(fs_Hz, dtype=float)
            Ts_s = 1.0 / fs_Hz
        else:
            Ts_s = np.asarray(Ts_s, dtype=float)

        avg_window = np.asarray(avg_window, dtype=float)

        with np.errstate(divide='ignore', invalid='ignore'):
            delta_f_Hz = 1.0 / (Ts_s * np.sqrt(avg_window))
            delta_f_kHz = delta_f_Hz / 1e3

        invalid = (Ts_s <= 0) | (avg_window <= 0)
        delta_f_kHz = np.where(invalid, np.nan, delta_f_kHz)

        return delta_f_kHz.item() if np.isscalar(fs_Hz) or np.isscalar(Ts_s) else delta_f_kHz
    
    def delta_G(self, vin_mV, i_dc_uA, fs_Hz=None, Ts_s=None, avg_window=1):
        """
        Estimate conductance resolution/uncertainty in uS.

        Uses:
            DeltaG ≈ |dG/df| * Deltaf
        """
        dG_df = self.dG_df(vin_mV, i_dc_uA)  # uS/kHz
        delta_f_kHz = self.delta_f_kHz(fs_Hz=fs_Hz, Ts_s=Ts_s, avg_window=avg_window)

        with np.errstate(invalid='ignore'):
            delta_G_uS = np.abs(dG_df) * delta_f_kHz

        return delta_G_uS.item() if np.isscalar(vin_mV) and np.isscalar(i_dc_uA) else delta_G_uS

    def delta_G_curve(self, i_dc_range, Ts_s=0.5, avg_window=1, vin_min=None, vin_max=None, n_points=200):
        """
        Returns ΔG(Vin, i_dc) = |dG/df| * Δf over a Vin sweep for each i_dc.

        Output:
            vin_smooth: shape (n_points,)
            delta_G:    shape (len(i_dc_range), n_points)
        """
        vin_smooth, dG_df = self.dG_df_curve(
            i_dc_range=i_dc_range,
            vin_min=vin_min,
            vin_max=vin_max,
            n_points=n_points
        )

        delta_f_kHz = self.delta_f_kHz(Ts_s=Ts_s, avg_window=avg_window)
        delta_G = np.abs(dG_df) * delta_f_kHz  # uS

        return vin_smooth, delta_G
    
    def delta_G_vs_idc(self, vin_mV, i_dc_range, fs_Hz=None, Ts_s=None, avg_window=1):
        """
        Conductance resolution as a function of i_dc for a fixed Vin.
        """
        i_dc_arr = np.asarray(i_dc_range, dtype=float)
        return i_dc_arr, self.delta_G(vin_mV, i_dc_arr, fs_Hz=fs_Hz, Ts_s=Ts_s, avg_window=avg_window)
    