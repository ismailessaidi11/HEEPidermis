from dataclasses import dataclass
import numpy as np

@dataclass
class WorkflowResult:
    f_osc_measured_kHz: float
    i_dc_uA: float
    vin_mV: float
    G_uS: float
    R_kohm: float
    i_dc_range: np.ndarray
    G_curve: np.ndarray
    dG_df: float
    dG_df_curve: np.ndarray
    delta_G: float
    delta_G_curve: np.ndarray
    idc_power_uW: float

def compute_workflow(model, f_osc_measured_kHz, i_dc_uA, fs_Hz):
    vin_mV = model.vin_from_fosc(f_osc_measured_kHz)

    G_uS = model.conductance(vin_mV, i_dc_uA)
    R_kohm = 1e3 / G_uS if not np.isnan(G_uS) and G_uS > 0 else np.nan

    i_dc_range = model.params.i_dc_range

    # 1D curves versus i_dc for the current Vin operating point
    G_curve = model.conductance(vin_mV, i_dc_range)
    dG_df_curve = model.dG_df(vin_mV, i_dc_range)
    delta_G_curve = model.delta_G(vin_mV, i_dc_range, fs_Hz=fs_Hz)

    # Scalar values at the selected operating point
    delta_G = model.delta_G(vin_mV, i_dc_uA, fs_Hz=fs_Hz)
    dG_df = model.dG_df(vin_mV, i_dc_uA)

    # Power consumption
    idc_power_uW = model.idc_power_uW(vin_mV, i_dc_uA)

    return WorkflowResult(
        f_osc_measured_kHz=f_osc_measured_kHz,
        i_dc_uA=i_dc_uA,
        vin_mV=vin_mV,
        G_uS=G_uS,
        R_kohm=R_kohm,
        i_dc_range=i_dc_range,
        G_curve=G_curve,
        dG_df=dG_df,
        dG_df_curve=dG_df_curve,
        delta_G=delta_G,
        delta_G_curve=delta_G_curve,
        idc_power_uW=idc_power_uW
    )