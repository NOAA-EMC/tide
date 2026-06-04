/**
 * @file grid.hpp
 * @brief TIDE ESMF Grid File Reader — load grids from ESMF mesh and grid spec files.
 *
 * Provides functions to read ESMF mesh files (unstructured) and ESMF grid
 * specification files (structured), producing SourceGrid or TargetGrid objects
 * suitable for Atlas-based regridding.
 *
 * ESMF Mesh files contain:
 * - Dimensions: nodeCount, elementCount, coordDim (=2), maxNodePElement
 * - Variables: nodeCoords(nodeCount, coordDim), elementConn(elementCount, maxNodePElement),
 *             numElementConn(elementCount)
 * - nodeCoords stores [lon, lat] pairs (ESMF convention: coordDim=0 is longitude)
 *
 * ESMF Grid Spec files contain:
 * - Variables: grid_center_lat(grid_size), grid_center_lon(grid_size)
 * - Optional: grid_corner_lat(grid_size, grid_corners), grid_corner_lon(grid_size, grid_corners)
 *
 * @section error_codes Error Codes
 * - 0: Success
 * - 850 (ErrorCode::EsmfFileNotFound): File not found or unreadable
 * - 851 (ErrorCode::EsmfMeshMissingVariable): Required mesh variable missing
 * - 852 (ErrorCode::EsmfGridSpecMissingVariable): Required grid spec variable missing
 *
 * Validates Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7
 */

#ifndef TIDE_GRID_HPP
#define TIDE_GRID_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include <tide/types.hpp>

namespace tide::grid {

/**
 * @brief Supported ESMF file types.
 */
enum class EsmfFileType {
    Mesh,       ///< Unstructured mesh (nodeCoords, elementConn, numElementConn)
    GridSpec    ///< Structured grid spec (grid_center_lat/lon, grid_corner_lat/lon)
};

/**
 * @brief Read an ESMF mesh file and produce a SourceGrid.
 *
 * Opens the NetCDF file at @p path, reads node coordinates (nodeCoords),
 * element connectivity (elementConn), and number of nodes per element
 * (numElementConn). Constructs an Unstructured-type SourceGrid with
 * cell center coordinates derived from element node averages and
 * connectivity stored in lat_bounds/lon_bounds for conservative regridding.
 *
 * ESMF mesh convention: nodeCoords is stored as (nodeCount, coordDim)
 * where coordDim[0] = longitude, coordDim[1] = latitude.
 *
 * @param path Path to the ESMF mesh NetCDF file.
 * @return SourceGrid with Unstructured type on success, Error on failure.
 *
 * @retval Error{850, ...} File not found or cannot be opened as NetCDF.
 * @retval Error{851, ...} Required variable (nodeCoords, elementConn,
 *                          or numElementConn) is missing from the file.
 */
[[nodiscard]] auto read_esmf_mesh(const std::filesystem::path& path)
    -> std::expected<SourceGrid, Error>;

/**
 * @brief Read an ESMF grid spec file and produce a SourceGrid.
 *
 * Opens the NetCDF file at @p path, reads grid center coordinates
 * (grid_center_lat, grid_center_lon) and optionally grid corner
 * coordinates (grid_corner_lat, grid_corner_lon).
 *
 * If corner coordinates are present, they are used as cell bounds
 * for conservative regridding.
 *
 * @param path Path to the ESMF grid spec NetCDF file.
 * @return SourceGrid with cell bounds from corner coordinates on success,
 *         Error on failure.
 *
 * @retval Error{850, ...} File not found or cannot be opened as NetCDF.
 * @retval Error{852, ...} Required variable (grid_center_lat or
 *                          grid_center_lon) is missing from the file.
 */
[[nodiscard]] auto read_esmf_grid_spec(const std::filesystem::path& path)
    -> std::expected<SourceGrid, Error>;

/**
 * @brief Read an ESMF file (mesh or grid spec) as a TargetGrid.
 *
 * Reads the specified ESMF file and produces a TargetGrid with
 * coordinates populated from the file. The file path is also stored
 * in the appropriate TargetGrid field (esmf_mesh or esmf_grid_spec).
 *
 * @param path Path to the ESMF NetCDF file.
 * @param type The file type (Mesh or GridSpec).
 * @return TargetGrid with coordinates and file path on success,
 *         Error on failure.
 *
 * @retval Error{850, ...} File not found or cannot be opened.
 * @retval Error{851, ...} Mesh file missing required variables.
 * @retval Error{852, ...} Grid spec file missing required variables.
 */
[[nodiscard]] auto read_esmf_as_target(const std::filesystem::path& path,
                                       EsmfFileType type)
    -> std::expected<TargetGrid, Error>;

} // namespace tide::grid

#endif // TIDE_GRID_HPP
