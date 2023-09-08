RTX Fluid
=========

Handles the rendering of a fluid. Does NOT simulate, only handles meshing and rendering. This allows various different simulation methods to be tested live. Currently the state is _owned_ by the renderer. In the future if alternative renderers are desired this may be further divided into a front facing GUI which owns state.

Changelog
=========
23/09/07 - Started project.
23/09/08 - Plugin system in place.