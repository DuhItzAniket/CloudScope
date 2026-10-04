"""CloudScope interim sky logger (phase P003).

Captures sky frames at a fixed interval with UTC timestamps and a JSON sidecar
per frame, so the data can be ingested by STRATIA. Replaced by the full
CloudScope capture sequencer in Stage C.
"""

__version__ = "0.1.0"
SIDECAR_SCHEMA = "cloudscope.sky_logger.frame/1"
SESSION_SCHEMA = "cloudscope.sky_logger.session/1"
