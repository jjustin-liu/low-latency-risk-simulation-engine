"""RiskSim Python bindings.

Thin shim package re-exporting the compiled ``_risksim`` nanobind extension,
which wraps the *shipping* C++ systemic-risk core. Importing this package gives
Python access to the exact same distribution, copula, contagion, and
Monte-Carlo kernels that run in production, so the model-validation suite
validates the code that ships rather than a re-implementation.
"""

from ._risksim import (  # noqa: F401
    norm_cdf,
    norm_inv_cdf,
    students_t_cdf,
    students_t_inv_cdf,
    t_copula_tail_dependence,
    empirical_var_es,
    gaussian_var_es,
    generate_network,
    network_from_marginals,
    simulate_system_losses,
    simulate_calibrated,
    NetworkHandle,
)

__all__ = [
    "norm_cdf",
    "norm_inv_cdf",
    "students_t_cdf",
    "students_t_inv_cdf",
    "t_copula_tail_dependence",
    "empirical_var_es",
    "gaussian_var_es",
    "generate_network",
    "network_from_marginals",
    "simulate_system_losses",
    "simulate_calibrated",
    "NetworkHandle",
]
