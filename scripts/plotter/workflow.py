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
    dG_df_curve: np.ndarray
    G_curve: np.ndarray

def compute_workflow(model, f_osc_measured_kHz, i_dc_uA):
    vin_mV = model.vin_from_fosc(f_osc_measured_kHz)
    G_uS = model.conductance(vin_mV, i_dc_uA)
    R_kohm = 1e3 / G_uS if not np.isnan(G_uS) and G_uS > 0 else np.nan

    G_curve = model.conductance(vin_mV, model.params.i_dc_range)
    dG_df_curve = model.dG_df(vin_mV, model.params.i_dc_range)

    return WorkflowResult(
        f_osc_measured_kHz=f_osc_measured_kHz,
        i_dc_uA=i_dc_uA,
        vin_mV=vin_mV,
        G_uS=G_uS,
        R_kohm=R_kohm,
        i_dc_range=model.params.i_dc_range,
        dG_df_curve=dG_df_curve,
        G_curve=G_curve,
    )