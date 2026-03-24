"""
Square Signal Generator with Interactive Frequency Control
Visualize real-time square wave changes with adjustable sampling points
Goal: Visualize the minimum frequency change that is detectable
and how sampling frequency affects signal reconstruction
"""

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider
from matplotlib.animation import FuncAnimation


def generate_square_wave(frequency, duration=1.0, num_samples=1000):
    """
    Generate a square wave signal (continuous).
    
    Args:
        frequency: Frequency in Hz
        duration: Duration of the signal in seconds
        num_samples: Number of sampling points
        
    Returns:
        t: Time array
        signal: Square wave signal
    """
    t = np.linspace(0, duration, num_samples)
    # Generate square wave: sign(sin(2*pi*f*t)) produces ±1 square wave
    signal = np.sign(np.sin(2 * np.pi * frequency * t))
    return t, signal


def get_sampled_points(frequency, sampling_freq, duration=1.0):
    """
    Get discrete sampling points based on sampling frequency.
    
    Args:
        frequency: Signal frequency in Hz
        sampling_freq: Sampling frequency in Hz
        duration: Duration of signal in seconds
        
    Returns:
        t_samples: Time points where samples are taken
        signal_samples: Signal values at sample points
    """
    # Generate sample times based on sampling frequency
    num_samples = int(sampling_freq * duration)
    if num_samples < 2:
        num_samples = 2
    t_samples = np.linspace(0, duration, num_samples, endpoint=True)
    # Sample the square wave at these points
    signal_samples = np.sign(np.sin(2 * np.pi * frequency * t_samples))
    return t_samples, signal_samples


class SquareWaveGenerator:
    def __init__(self, initial_freq=1.0, max_freq=50.0, initial_samp_freq=100.0, 
                 max_samp_freq=500.0, duration=2.0):
        """
        Initialize the square wave generator with interactive control.
        
        Args:
            initial_freq: Initial signal frequency in Hz
            max_freq: Maximum signal frequency for slider in Hz
            initial_samp_freq: Initial sampling frequency in Hz
            max_samp_freq: Maximum sampling frequency in Hz
            duration: Time window to display in seconds
        """
        self.initial_freq = initial_freq
        self.max_freq = max_freq
        self.initial_samp_freq = initial_samp_freq
        self.max_samp_freq = max_samp_freq
        self.duration = duration
        self.frequency = initial_freq
        self.sampling_freq = initial_samp_freq
        
        # Create figure and axis
        self.fig, self.ax = plt.subplots(figsize=(14, 7))
        plt.subplots_adjust(bottom=0.35)
        
        # Generate initial continuous signal (for reference)
        self.t_continuous, self.signal_continuous = generate_square_wave(
            self.frequency, self.duration, 5000
        )
        
        # Plot continuous signal as background reference
        self.line_continuous, = self.ax.plot(
            self.t_continuous, self.signal_continuous, 
            lw=3, color='gray', alpha=0.4, label='Real Signal (Continuous)', zorder=1
        )
        
        # Get and plot initial sampled points
        self.t_samples, self.signal_samples = get_sampled_points(
            self.frequency, self.sampling_freq, self.duration
        )
        
        # Connect samples with lines (displayed on top)
        self.line_samples, = self.ax.plot(
            self.t_samples, self.signal_samples, 
            lw=2.5, color='red', alpha=0.9, linestyle='-', label='Sampled Signal', zorder=4
        )
        
        # Plot sampled points (on top)
        self.scatter = self.ax.scatter(
            self.t_samples, self.signal_samples, 
            s=120, color='red', marker='o', zorder=5, label='Sample Points', edgecolors='darkred', linewidth=1.5
        )
        
        self.ax.set_xlabel('Time (s)', fontsize=12)
        self.ax.set_ylabel('Amplitude', fontsize=12)
        self.ax.set_title(f'Square Wave: f={self.frequency:.2f} Hz, f_s={self.sampling_freq:.1f} Hz', 
                         fontsize=14)
        self.ax.set_ylim([-1.5, 1.5])
        self.ax.grid(True, alpha=0.3)
        self.ax.legend(loc='upper right', fontsize=10)
        
        # Add frequency info text
        self.info_text = self.ax.text(0.02, 0.95, '', transform=self.ax.transAxes,
                                     verticalalignment='top', fontsize=10,
                                     bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.7))
        
        # Create signal frequency slider
        ax_freq_slider = plt.axes([0.2, 0.20, 0.6, 0.03])
        self.slider_freq = Slider(
            ax_freq_slider, 'Signal Frequency (Hz)', 0.1, max_freq,
            valinit=initial_freq, valstep=0.1, color='blue'
        )
        self.slider_freq.on_changed(self.update_signal)
        
        # Create sampling frequency slider
        ax_samp_slider = plt.axes([0.2, 0.14, 0.6, 0.03])
        self.slider_samp = Slider(
            ax_samp_slider, 'Sampling Frequency (Hz)', 2, max_samp_freq,
            valinit=initial_samp_freq, valstep=1, color='red'
        )
        self.slider_samp.on_changed(self.update_signal)
        
        # Create comparison text box
        self.comparison_text = self.ax.text(0.02, 0.83, '', transform=self.ax.transAxes,
                                           verticalalignment='top', fontsize=9,
                                           bbox=dict(boxstyle='round', facecolor='lightgreen', alpha=0.7))
    
    def update_signal(self, val):
        """Update the signal when sliders change."""
        new_freq = self.slider_freq.val
        new_samp_freq = self.slider_samp.val
        
        freq_changed = (new_freq != self.frequency)
        samp_freq_changed = (new_samp_freq != self.sampling_freq)
        
        self.frequency = new_freq
        self.sampling_freq = new_samp_freq
        
        # Regenerate continuous signal if frequency changed
        if freq_changed:
            self.t_continuous, self.signal_continuous = generate_square_wave(
                self.frequency, self.duration, 5000
            )
            self.line_continuous.set_ydata(self.signal_continuous)
        
        # Get new sampled points
        self.t_samples, self.signal_samples = get_sampled_points(
            self.frequency, self.sampling_freq, self.duration
        )
        
        # Update scatter plot
        self.scatter.set_offsets(np.c_[self.t_samples, self.signal_samples])
        
        # Update line plot
        self.line_samples.set_data(self.t_samples, self.signal_samples)
        
        # Update title
        self.ax.set_title(
            f'Square Wave: f={self.frequency:.2f} Hz, f_s={self.sampling_freq:.1f} Hz',
            fontsize=14
        )
        
        # Calculate Nyquist frequency
        nyquist_freq = self.sampling_freq / 2
        aliasing_risk = "⚠️ ALIASING RISK" if self.frequency > nyquist_freq else "✓ OK"
        
        # Update info text
        num_samples = int(self.sampling_freq * self.duration)
        period = 1 / self.frequency if self.frequency > 0 else 0
        samples_per_period = period * self.sampling_freq if period > 0 else 0
        
        self.info_text.set_text(
            f'Signal Period: {period:.4f} s\n'
            f'Signal Samples: {num_samples}\n'
            f'Samples/Period: {samples_per_period:.1f}'
        )
        
        # Update comparison info
        self.comparison_text.set_text(
            f'Nyquist Frequency: {nyquist_freq:.1f} Hz {aliasing_risk}\n'
            f'Signal/Nyquist Ratio: {self.frequency/nyquist_freq:.2f}\n'
            f'Sampling Period: {1/self.sampling_freq*1000:.2f} ms'
        )
        
        self.fig.canvas.draw_idle()
    
    def show(self):
        """Display the interactive plot."""
        plt.show()


if __name__ == '__main__':
    # Create and display the square wave generator
    # Parameters can be adjusted to test detectability of frequency changes
    generator = SquareWaveGenerator(
        initial_freq=5.0,           # Start at 5 Hz
        max_freq=50.0,              # Allow up to 50 Hz signal
        initial_samp_freq=100.0,    # Start at 100 Hz sampling
        max_samp_freq=500.0,        # Allow up to 500 Hz sampling
        duration=2.0                # Show 2 seconds of signal
    )
    generator.show()
