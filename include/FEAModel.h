#pragma once
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <atomic>
#include <glm/glm.hpp>
#include "FEAData.h"
#include "BuiltInShader.h"
#include "IGeometryLoader.h"
#include "MeshQuality.h"

// Forward-declare only — keeps OCC headers out of every TU that includes FEAModel.h.
// BRepHandle.h is included in FEAModel.cpp where Impl is complete (required by
// unique_ptr destructor and move operations).
class BRepHandle;
namespace Toolpath { struct ToolpathModel; }  // (ToolpathModel.h)

struct ForceArrow {
    glm::vec3 start;
    glm::vec3 end;
};

// FE-facing layered-FDM data model. planeCoords (slab boundaries,
// ascending, nSlabs+1) + buildAxis + thickness/grouping are filled by the slicer
// itself. elemSlabIndex / elemRegion are per-tet and filled by the slab mesher.
struct LayerStack {
    int   buildAxis = 2;                 // 0=X 1=Y 2=Z
    float physicalLayerThickness = 0.0f; // absolute units, post import scale
    int   layersPerSlab = 1;             // grouping factor k
    std::vector<float>   planeCoords;    // slab boundaries, ascending (nSlabs+1)
    std::vector<int>     elemSlabIndex;  // per-tet (filled by the slab mesher)
    std::vector<uint8_t> elemRegion;     // 0=infill 1=wall (filled by the slab mesher)
    int nSlabs() const {
        return planeCoords.empty() ? 0 : static_cast<int>(planeCoords.size()) - 1;
    }

    // Weld-interface registry (general path): a weld interface
    // between slab s (its top ring) and slab s+1 (its bottom triangulation).
    // The toolpath lane meshes every slab with INDEPENDENT node rings (sections
    // differ per layer), so the interface is a set of barycentric point ties
    // that the solver assembles as penalty springs. The delamination pass
    // later releases individual ties; until then `released` stays false.
    struct WeldInterface {
        int slabBelow = 0;                 // couples slabBelow(top) <-> slabBelow+1(bottom)
        struct Tie {
            unsigned nodeTop;              // node on slab s top ring
            unsigned triBottom[3];         // containing tri nodes on slab s+1 bottom ring
            float    bary[3];              // barycentric weights of nodeTop in triBottom
            bool     released = false;     // flips this
        };
        std::vector<Tie> ties;
        float areaMM2      = 0.0f;         // interface section area (for k_pen)
        float thicknessMM  = 0.0f;         // slab thickness at this interface
    };
    std::vector<WeldInterface> interfaces;
    float tieAlpha = 100.0f;               // k_pen = alpha * E_z * A_tie / t
};

class FEAModel {
public:
    unsigned int VAO, VBO, EBO;
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    FEAParams params;
    bool needsUpdate = true;
    float bboxVolume = 1000.0f; // NEW: Stores overall part volume
    // uniform model-space scale applied to the last loaded geometry
    // (3.0/maxDim in processRawGeometry; 1.0 for the un-rescaled preset cube).
    // Converts print-unit layerThickness -> model units for the slicer.
    float importScale = 1.0f;

    // real-world physical size in MILLIMETRES (the canonical internal
    // length unit), preserved across the 3-unit render normalisation. The renderer
    // uses model-space coords; the slicer and the FEA solver use physical mm.
    //   modelToMM : multiply a model-space length to get millimetres
    //               (= fileUnitToMM / importScale).
    glm::vec3 physicalMinMM = glm::vec3(-0.5f);
    glm::vec3 physicalMaxMM = glm::vec3( 0.5f);
    float     modelToMM     = 1.0f;
    glm::vec3 physicalSizeMM() const { return physicalMaxMM - physicalMinMM; }
    // max nodal displacement of the last solve in PHYSICAL mm
    // (real-world, un-exaggerated). 0 until a solve runs.
    float     physicalMaxDispMM = 0.0f;

    std::string loadedFileName = "";
    std::string lastLoadedFormat = "";  // "STL" | "3MF" | "STEP" | ""
    int         lastLoadedObjectCount = 0; // number of objects found in last load

    // retained analytic B-rep (non-null when last load was STEP).
    std::unique_ptr<BRepHandle> brep;
    bool hasBRep() const { return brep != nullptr; }

    // parsed Bambu .gcode.3mf toolpath (non-null when the last
    // load was a sliced gcode export). The authoritative FDM geometry source;
    // until the toolpath slab mesh is built, the render surface is only the
    // part-bbox shell synthesized by loadGcode3mf().
    std::unique_ptr<Toolpath::ToolpathModel> toolpath;
    bool hasToolpath() const { return toolpath != nullptr; }
    bool hasVolumetricMesh = false;
    bool showVolumetricMesh = false;
    
    // NEW: FEA state tracking
    bool hasDeformation = false;
    bool showDeformedMesh = true;
    bool showAppliedForceField = false;
    std::vector<ForceArrow> appliedForces;
    std::vector<float> nodalDisplacementMagnitudes;
    // TRUE physical displacement per node (mm), no display
    // exaggeration — the harness probes read THIS, never deformedPositions
    // (which bake in visualScale and are render-only). Filled by the scaled
    // linear/fracture solve path; empty when the last solve didn't compute it.
    std::vector<glm::vec3> nodalDispMM;
    std::vector<float> nodalForceMagnitudes;
    float displacementMin = 0.0f;
    float displacementMax = 0.0f;
    float appliedForceMin = 0.0f;
    float appliedForceMax = 0.0f;
    float totalAppliedForce = 0.0f;
    float appliedForcePerNode = 0.0f;
    glm::vec3 currentMinBounds = glm::vec3(-0.5f);
    glm::vec3 currentMaxBounds = glm::vec3(0.5f);
    
    std::vector<Vertex> surfaceVertices;
    std::vector<unsigned int> surfaceIndices;
    std::vector<Vertex> volumetricVertices;
    std::vector<unsigned int> volumetricIndices;
    std::vector<unsigned int> tetrahedra; // NEW: stores the 4 node indices of each tetrahedron
    std::vector<unsigned int> tetrahedraQuadratic; // 10 node indices per tet (Tet10)
    bool hasQuadraticMesh = false;

    // Brittle-fracture state (set by FEASolver::solveBrittleFracture).
    // Length = nElems.  1 = active, 0 = removed.
    std::vector<uint8_t> elementAlive;
    // Fracture iteration at which each element died; -1 means still alive.
    std::vector<int>     elementFailureIter;
    // Failure mode that killed each element (0=alive, 1=interlayer-tension,
    // 2=interlayer-shear, 3=intralayer).  Only meaningful when useFdmAnisotropy
    // is true; otherwise all killed elements carry mode 0.
    std::vector<uint8_t> elementFailureMode;
    // von Mises stress each element carried at the iteration it died
    // (0 for elements still alive). Filled by solveBrittleFracture from sigmaVM.
    std::vector<float>   elementVonMisesAtDeath;

    // fracture result visualization controls + render buffers.
    // fractureViewMode selects what colours the result: 1=deform (per-vertex
    // displacement on the surviving body), 3=failure mode, 4=crack order
    // (failure iteration), 5=von Mises at death. Modes 3/4/5 colour the DEAD
    // elements (drawn in a second pass); the alive body goes grey so the crack
    // pattern reads clearly. (Numbering reserved through 7.)
    int fractureViewMode = 1;
    enum FractureDeadView { DEAD_HIDDEN = 0, DEAD_GHOST = 1, DEAD_COLORED = 2 };
    FractureDeadView fractureDeadView = DEAD_HIDDEN;
    // Dead-element geometry uses its own VBO/VAO with DUPLICATED vertices so each
    // element carries a single flat per-element value (no blending with neighbours)
    // and the alive draw path stays untouched. Rebuilt in buildBuffers().
    std::vector<Vertex>        deadVertices;
    std::vector<unsigned int>  deadIndices;
    unsigned int deadVAO = 0, deadVBO = 0, deadEBO = 0;
    float deadScalarMin = 0.0f;   // colour-range for dead-pass heatmaps (modes 4/5)
    float deadScalarMax = 1.0f;

    // layered-FDM data model + 2-D section preview.
    // `layers` is the FE-facing slab model (filled by setLayerStack after slicing).
    std::unique_ptr<LayerStack> layers;
    bool hasLayerStack() const { return layers != nullptr; }
    // Slice preview render state. The preview is a dedicated GL_LINES buffer
    // (separate VAO so the model buffers are never disturbed) rebuilt only when the
    // section or layer slider changes. Drawn as an overlay after the model.
    bool          showSlicePreview = false;
    unsigned int  sliceVAO = 0, sliceVBO = 0;
    int           sliceLineVertexCount = 0;

    // 3-D load-arrow overlay (same dedicated-GL_LINES pattern as
    // the slice preview). Geometry comes from `appliedForces`, which the
    // solver's load presets fill from the SAME node sets and direction the
    // load actually uses — the arrow is the applied load, never a guess.
    unsigned int  arrowVAO = 0, arrowVBO = 0;
    int           arrowLineVertexCount = 0;
    size_t        arrowSourceCount = SIZE_MAX;  // appliedForces.size() at last build
    void buildForceArrowBuffers();              // appliedForces -> line VBO
    void drawForceArrows(BuiltInShader& shader); // gated on showAppliedForceField

    // Toolpath preview: renders the ACTUAL extrusion moves of a loaded
    // .gcode.3mf as feature-coloured GL_LINES (the slicer-frontend look),
    // replacing the placeholder bbox shell until a volume mesh is generated.
    // One VBO holds all segments grouped by feature; tpRanges records the
    // (first, count, colour) draw ranges.
    struct TpDrawRange { int first = 0; int count = 0; glm::vec3 color{0.7f}; };
    unsigned int  tpVAO = 0, tpVBO = 0;
    int           tpLineVertexCount = 0;
    std::vector<TpDrawRange> tpRanges;
    bool          showToolpathPreview = false;
    void buildToolpathPreview();                 // toolpath segments -> line VBO
    void drawToolpathPreview(BuiltInShader& shader);

    // Sectional view: fragments with world Z below sectionZModel are discarded
    // (shader uniform) and a translucent grey XY plane is drawn at the cut.
    bool          sectionEnabled = false;
    float         sectionZModel  = 0.0f;         // model-space cut height
    unsigned int  secVAO = 0, secVBO = 0;
    void drawSectionPlane(BuiltInShader& shader); // no-op unless sectionEnabled

    // Async compute support: while a worker thread runs a solver/mesher on this
    // model, GL uploads are deferred — buildBuffers() becomes a no-op (worker
    // threads own no GL context, and the render thread keeps drawing the last
    // uploaded state untouched). The main thread clears the flag and calls
    // buildBuffers() once when the job completes.
    bool deferGLUpload = false;
    // TetGen itself is a blocking third-party call, but these hooks provide
    // honest stage progress and allow a cancellation requested during TetGen
    // to discard its output before any live model state is replaced.
    std::atomic<float>* computeProgressOut = nullptr;
    std::atomic<bool>*  computeCancelRequested = nullptr;

    int  nLinearNodes = 0;  // number of original Tet4 nodes (before mid-edge insertion)
    // Maps canonical edge (min,max) -> mid-edge node index.
    std::map<std::pair<unsigned int,unsigned int>, unsigned int> edgeToMidNode;
    std::vector<glm::vec3> originalVolumetricPositions; // NEW: stores initial positions before deformation
    std::vector<glm::vec3> deformedPositions; // NEW: stores displaced positions

    // snapshot of the input surface taken immediately before TetGen.
    // Compared against the vol-mesh boundary to compute Hausdorff + normal dev.
    MeshQuality::RefSurface refSurfaceForFidelity;

    FEAModel();
    // Destructor must be in .cpp where BRepHandle is complete (pImpl pattern).
    ~FEAModel();

    void buildBuffers();
    void generateCube();
    void generate_face(glm::vec3 normal, glm::vec3 u, glm::vec3 v, int sub);
    // --- Format-specific load entry points ---
    // All route through processRawGeometry() for identical post-processing.
    bool loadSTL(const std::string& filepath);
    bool load3MF(const std::string& filepath);
    bool loadSTEP(const std::string& filepath); // STEP + B-rep retention
    // Bambu sliced export -> ToolpathModel + bbox-shell surface.
    bool loadGcode3mf(const std::string& filepath, bool partOnly = false);

    // Load any supported format (dispatches by extension).
    bool loadFile(const std::string& filepath);
    void draw(BuiltInShader& shader, glm::vec3 viewPos);
    bool generateVolumetricMesh();
    // Generates mid-edge nodes from the existing Tet4 mesh, populating
    // tetrahedraQuadratic (10*nElems), extending originalVolumetricPositions
    // and volumetricVertices, and setting hasQuadraticMesh = true.
    void generateMidEdgeNodes();

private:
    // Shared post-processing after any geometry loader:
    // welding (done by loaders), optional decimation, normal recomputation,
    // bbox centering/scaling, and surface buffer upload.
    // On success fills surfaceVertices/surfaceIndices, resets volumetric state,
    // sets loadedFileName / lastLoadedFormat / lastLoadedObjectCount, and
    // calls buildBuffers().
    bool processRawGeometry(LoadedGeometry& geo, const std::string& formatTag);

    // rebuild the dead-element render buffers (deadVertices/Indices)
    // from the current elementAlive/Failure* data + fractureViewMode. No-op when
    // no fracture state is present.
    void rebuildDeadElementBuffers();

public:
    // populate the FE-facing LayerStack (slab boundaries + grouping).
    // Called by both the UI SLICE controls and the headless harness (same path).
    void setLayerStack(int buildAxis, float physicalLayerThickness, int layersPerSlab,
                       const std::vector<float>& slabBoundaryCoords);
    // Rebuild the preview line buffer from already-projected 3-D segment endpoints
    // (consecutive pairs = one GL line). Empty input clears the preview.
    void buildSlicePreview(const std::vector<glm::vec3>& segmentEndpoints);
    // Draw the preview overlay (no-op unless showSlicePreview and a buffer exist).
    void drawSlicePreview(BuiltInShader& shader);

    void updateScalarFieldData();
    void updateBounds();
    int getActiveScalarMode() const;
    float getActiveScalarMin() const;
    float getActiveScalarMax() const;
};
