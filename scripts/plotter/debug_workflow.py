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
    df_osc_adev_Hz: np.ndarray
    df_osc_sampling_Hz: np.ndarray
    df_osc_Hz: np.ndarray
    dVin_mV: np.ndarray
    delta_G_uS: float
    delta_G_curve: np.ndarray
    idc_power_uW: float

def compute_workflow(model, f_osc_measured_kHz, i_dc_uA, fs_Hz):
    vin_mV = model.vin_from_fosc(f_osc_measured_kHz)
    i_dc_range = model.params.i_dc_range

    G_uS = model.conductance(vin_mV, i_dc_uA)
    R_kohm = 1e3 / G_uS if not np.isnan(G_uS) and G_uS > 0 else np.nan
    # 1D curves versus i_dc for the current Vin operating point
    G_curve = model.conductance(vin_mV, i_dc_range)

    # computing the frequency error contributions
    df_osc_adev_Hz = model.df_osc_adev_Hz(vin_mV=vin_mV, fs_Hz=fs_Hz)
    df_osc_sampling_Hz = model.df_osc_sampling_Hz(fs_Hz=fs_Hz)
    df_osc_Hz = model.df_osc_Hz(vin_mV=vin_mV, fs_Hz=fs_Hz)

    # computing the V_in error
    dVin_mV = model.dVin_mV(df_osc_Hz=df_osc_Hz, vin_mV=vin_mV)

    # Scalar values at the selected operating point
    delta_G_uS = model.delta_G_uS(vin_mV = vin_mV, i_dc_uA=i_dc_uA, fs_Hz=fs_Hz)
    delta_G_curve = model.delta_G_uS(vin_mV=vin_mV, i_dc_uA=i_dc_range, fs_Hz=fs_Hz)

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
        df_osc_adev_Hz=df_osc_adev_Hz,
        df_osc_sampling_Hz=df_osc_sampling_Hz,
        df_osc_Hz=df_osc_Hz,
        dVin_mV=dVin_mV,
        delta_G_uS=delta_G_uS,
        delta_G_curve=delta_G_curve,
        idc_power_uW=idc_power_uW
    )