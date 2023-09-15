RTX Fluid
=========

Handles the rendering of a fluid. Does NOT simulate, only handles rendering. This allows various different simulation methods to be tested live.

Simulation state is shared using a hand written in-memory database. The database makes the following assumptions:

- The number of tables in a project is small and doesn't grow much over time. There is currently a hard limit of 512 tables.
- There are relatively few processes needing write access to the same table. A process currently needs exclusive access to a section of the table for writing,
  but caching changes would be more performant in write-heavy scenarios.


Compiling
=========

Only Windows builds are currently supported (with intention to eventually port to Linux). To compile, run the following commands
from within this folder:

    mkdir build
    cd build
    cmake ..

Open the vcproj once generated and build the `flip` and `rtxfluid` solutions. Run the `rtxfluid` solution.

Alternatively you can use WSL to cross compile using mingw. To do this, simply run `make` within this folder.


Changelog
=========

    23/09/07 - Started project.
    23/09/08 - Plugin system in place.
    23/09/15 - Initial draft of in-memory shared database.