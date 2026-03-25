from ipywidgets import FloatSlider, VBox, HBox, Layout, HTML, widgets, interactive_output, ToggleButton
from IPython.display import display
import matplotlib.pyplot as plt
from matplotlib.figure import Figure
from debug_pannels import *
from debug_workflow import compute_workflow
from forward_pannels import *
from forward_workflow import *
from VCO_model import *


def debug_plotter(VCO_model):
    f_osc_slider = FloatSlider(
        value=820, min=0, max=1200, step=10,
        description='f_osc (kHz):',
        continuous_update=True,
        layout=Layout(width='300px')
    )

    i_dc_slider = FloatSlider(
        value=1.0, min=0.1, max=10, step=0.1,
        description='i_dc (μA):',
        continuous_update=True,
        layout=Layout(width='300px')
    )

    fs_Hz_slider = FloatSlider(
        value=1.0, min=0.5, max=20, step=0.5,
        description='fs (Hz):',
        continuous_update=True,
        layout=Layout(width='300px')
    )

    controls = VBox(
        [
            HTML("<h4>Debug controls</h4>"),
            f_osc_slider,
            i_dc_slider,
            fs_Hz_slider,
        ],
        layout=Layout(
            width='320px',
            padding='10px',
            border='1px solid #ddd'
        )
    )

    controls_wrapper = VBox(
        [controls],
        layout=Layout(
            width='340px',
            min_width='340px',
            height='850px',
            justify_content='center'
        )
    )

    def _plot(f_osc_measured_kHz, i_dc_uA, fs_Hz):
        result = compute_workflow(
            VCO_model,
            f_osc_measured_kHz,
            i_dc_uA,
            fs_Hz
        )

        fig = plt.figure(figsize=(12, 10), constrained_layout=True)
        gs = fig.add_gridspec(3, 2)

        plot_fosc_model(
            VCO_model,
            mode="measurement",
            ax=fig.add_subplot(gs[0, 0]),
            result=result
        )
        plot_vin_text(
            fig.add_subplot(gs[0, 1]),
            VCO_model,
            result
        )
        plot_conductance_text(
            fig.add_subplot(gs[1, 0]),
            result
        )
        plot_df_osc_components(
            fig.add_subplot(gs[1, 1]),
            VCO_model,
            result,
            fs_Hz
        )
        plot_delta_G(
            fig.add_subplot(gs[2, 0]),
            VCO_model,
            result,
            fs_Hz
        )
        plot_power(
            fig.add_subplot(gs[2, 1]),
            VCO_model,
            result
        )

        fig.suptitle(
            'Integrated Measurement Workflow: $f_{osc} \\rightarrow V_{in} \\rightarrow G$',
            fontsize=14,
            fontweight='bold'
        )

        plt.show()
        plt.close(fig)

        print_analysis(VCO_model, result)

    ui = interactive_output(
        _plot,
        {
            'f_osc_measured_kHz': f_osc_slider,
            'i_dc_uA': i_dc_slider,
            'fs_Hz': fs_Hz_slider
        }
    )

    display(HBox([controls_wrapper, ui], layout=Layout(align_items='center')))

def forward_plotter(model, variance=1, avg_window=1):
    G_slider = FloatSlider(
        value=8.0, min=0.5, max=20.0, step=0.5,
        description='G (μS):',
        continuous_update=True,
        layout=Layout(width='300px')
    )

    i_dc_slider = FloatSlider(
        value=1.0, min=0.1, max=10.0, step=0.1,
        description='i_dc (μA):',
        continuous_update=True,
        layout=Layout(width='300px')
    )

    fs_slider = FloatSlider(
        value=1.0, min=0.5, max=20.0, step=0.5,
        description='fs (Hz):',
        continuous_update=True,
        layout=Layout(width='300px')
    )
    variance_on = ToggleButton(description='avar OFF', value=False, layout=Layout(width='80px', height='30px'))
    
    def update_variance_description(change):
        variance_on.description = 'avar ON' if change['new'] else 'avar OFF'
    
    variance_on.observe(update_variance_description, names='value')
    
    def update_i_dc_max(change):
        """Update i_dc slider max: i_dc_max = G × (V_dd - V_min)"""
        G_value = change['new']
        max_i_dc = model.i_dc_max(G_value)
        i_dc_slider.max = max_i_dc
        if i_dc_slider.value > max_i_dc:
            i_dc_slider.value = max_i_dc
    
    G_slider.observe(update_i_dc_max, names='value')
    
    variance_container = HBox(
        [variance_on],
        layout=Layout(justify_content='center', width='320px')
    )

    controls = VBox(
        [
            HTML("<h4>Forward design controls</h4>"),
            G_slider,
            i_dc_slider,
            fs_slider,
            variance_container,
        ],
        layout=Layout(
            width='320px',
            padding='10px',
            border='1px solid #ddd'
        )
    )

    controls_wrapper = VBox(
        [controls],
        layout=Layout(
            width='340px',
            min_width='340px',
            height='900px',
            justify_content='center'
        )
    )

    def _plot(G_uS, i_dc_uA, fs_Hz, variance_enabled):
        inp = forward_input(G_uS=G_uS, i_dc_uA=i_dc_uA, fs_Hz=fs_Hz)
        active_variance = variance if variance_enabled else 0
        result = forward_compute(model=model, input=inp, variance=active_variance, avg_window=avg_window)

        fig = plt.figure(figsize=(13, 10), constrained_layout=True)
        gs = fig.add_gridspec(3, 2)

        plot_forward_vco_point(fig.add_subplot(gs[0, 0]), model, result)
        plot_forward_summary(fig.add_subplot(gs[0, 1]), result, model)
        plot_forward_df_components(fig.add_subplot(gs[1, 0]), result)
        plot_forward_outputs(fig.add_subplot(gs[1, 1]), result)
        plot_forward_tradeoff(fig.add_subplot(gs[2, 0]), model, result,
                              variance=variance, avg_window=avg_window)

        fig.suptitle(
            r'Forward workflow: $(G, i_{dc}, f_s)\rightarrow(V_{in}, f_{osc}, \Delta f, \Delta V_{in})\rightarrow(\Delta G, P)$',
            fontsize=14,
            fontweight='bold'
        )
        plt.show()
        plt.close(fig)

    ui = interactive_output(
        _plot,
        {
            'G_uS': G_slider,
            'i_dc_uA': i_dc_slider,
            'fs_Hz': fs_slider,
            'variance_enabled': variance_on
        }
    )

    display(HBox([controls_wrapper, ui], layout=Layout(align_items='center')))