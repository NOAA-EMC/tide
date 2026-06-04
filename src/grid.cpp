/**
 * @file grid.cpp
 * @brief Implementation of the TIDE ESMF Grid File Reader.
 *
 * Reads ESMF mesh files and ESMF grid specification files using the
 * NetCDF-C API, producing SourceGrid or TargetGrid objects for use
 * with Atlas-based regridding.
 *
 * Validates Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7
 */

#include <tide/grid.hpp>
#include <tide/error.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <netcdf.h>

namespace tide::grid {

namespace {

/// @brief RAII guard for NetCDF file handles.
struct NcGuard {
    int ncid{-1};

    ~NcGuard() {
        if (ncid >= 0) {
            nc_close(ncid);
        }
    }

    NcGuard() = default;
    NcGuard(const NcGuard&) = delete;
    NcGuard& operator=(const NcGuard&) = delete;
    NcGuard(NcGuard&& o) noexcept : ncid(o.ncid) { o.ncid = -1; }
    NcGuard& operator=(NcGuard&& o) noexcept {
        if (this != &o) {
            if (ncid >= 0) nc_close(ncid);
            ncid = o.ncid;
            o.ncid = -1;
        }
        return *this;
    }
};

} // anonymous namespace

auto read_esmf_mesh(const std::filesystem::path& path)
    -> std::expected<SourceGrid, Error> {

    // Open the NetCDF file
    NcGuard guard;
    int rc = nc_open(path.c_str(), NC_NOWRITE, &guard.ncid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfFileNotFound),
            std::string("Cannot open ESMF mesh file: ") + path.string() +
                " (" + nc_strerror(rc) + ")",
            "grid"});
    }

    const int ncid = guard.ncid;

    // Read nodeCount dimension
    int node_count_dimid{-1};
    rc = nc_inq_dimid(ncid, "nodeCount", &node_count_dimid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "ESMF mesh file missing dimension 'nodeCount': " + path.string(),
            "grid"});
    }

    std::size_t node_count{0};
    rc = nc_inq_dimlen(ncid, node_count_dimid, &node_count);
    if (rc != NC_NOERR || node_count == 0) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "ESMF mesh file has invalid 'nodeCount' dimension: " + path.string(),
            "grid"});
    }

    // Read elementCount dimension
    int elem_count_dimid{-1};
    rc = nc_inq_dimid(ncid, "elementCount", &elem_count_dimid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "ESMF mesh file missing dimension 'elementCount': " + path.string(),
            "grid"});
    }

    std::size_t element_count{0};
    rc = nc_inq_dimlen(ncid, elem_count_dimid, &element_count);
    if (rc != NC_NOERR || element_count == 0) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "ESMF mesh file has invalid 'elementCount' dimension: " + path.string(),
            "grid"});
    }

    // Read coordDim dimension
    int coord_dim_dimid{-1};
    rc = nc_inq_dimid(ncid, "coordDim", &coord_dim_dimid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "ESMF mesh file missing dimension 'coordDim': " + path.string(),
            "grid"});
    }

    std::size_t coord_dim{0};
    nc_inq_dimlen(ncid, coord_dim_dimid, &coord_dim);

    // Read maxNodePElement dimension
    int max_node_dimid{-1};
    rc = nc_inq_dimid(ncid, "maxNodePElement", &max_node_dimid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "ESMF mesh file missing dimension 'maxNodePElement': " + path.string(),
            "grid"});
    }

    std::size_t max_node_p_element{0};
    nc_inq_dimlen(ncid, max_node_dimid, &max_node_p_element);

    // Read nodeCoords variable (nodeCount, coordDim)
    // ESMF convention: coordDim[0] = longitude, coordDim[1] = latitude
    int varid{-1};
    rc = nc_inq_varid(ncid, "nodeCoords", &varid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "ESMF mesh file missing variable 'nodeCoords': " + path.string(),
            "grid"});
    }

    // nodeCoords is stored as (nodeCount, coordDim) — a flat array of
    // [lon0, lat0, lon1, lat1, ...]
    std::vector<double> node_coords(node_count * coord_dim);
    rc = nc_get_var_double(ncid, varid, node_coords.data());
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "Failed to read 'nodeCoords': " + std::string(nc_strerror(rc)),
            "grid"});
    }

    // Read elementConn variable (elementCount, maxNodePElement)
    rc = nc_inq_varid(ncid, "elementConn", &varid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "ESMF mesh file missing variable 'elementConn': " + path.string(),
            "grid"});
    }

    std::vector<int> element_conn(element_count * max_node_p_element);
    rc = nc_get_var_int(ncid, varid, element_conn.data());
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "Failed to read 'elementConn': " + std::string(nc_strerror(rc)),
            "grid"});
    }

    // Read numElementConn variable (elementCount)
    rc = nc_inq_varid(ncid, "numElementConn", &varid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "ESMF mesh file missing variable 'numElementConn': " + path.string(),
            "grid"});
    }

    std::vector<int> num_element_conn(element_count);
    rc = nc_get_var_int(ncid, varid, num_element_conn.data());
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfMeshMissingVariable),
            "Failed to read 'numElementConn': " + std::string(nc_strerror(rc)),
            "grid"});
    }

    // Extract node latitudes and longitudes from nodeCoords
    // ESMF convention: nodeCoords(nodeCount, coordDim) with [lon, lat] ordering
    std::vector<double> node_lons(node_count);
    std::vector<double> node_lats(node_count);
    for (std::size_t i = 0; i < node_count; ++i) {
        node_lons[i] = node_coords[i * coord_dim];       // coordDim[0] = lon
        node_lats[i] = node_coords[i * coord_dim + 1];   // coordDim[1] = lat
    }

    // Compute element center coordinates (average of node coordinates)
    // and store connectivity information in bounds arrays for
    // conservative regridding via Atlas.
    std::vector<double> elem_lats(element_count);
    std::vector<double> elem_lons(element_count);

    // For unstructured grids, we store the connectivity as flattened
    // polygon vertex coordinates. lat_bounds and lon_bounds store
    // vertex coordinates for each element sequentially:
    // [elem0_node0_lat, elem0_node1_lat, ..., elem1_node0_lat, ...]
    // This allows Atlas to reconstruct cell polygons.
    std::vector<double> lat_bounds;
    std::vector<double> lon_bounds;

    for (std::size_t e = 0; e < element_count; ++e) {
        const int n_nodes = num_element_conn[e];
        double sum_lon = 0.0;
        double sum_lat = 0.0;

        for (int n = 0; n < n_nodes; ++n) {
            // elementConn is 1-based (ESMF/Fortran convention)
            const int node_idx = element_conn[e * max_node_p_element + n] - 1;
            const double lon = node_lons[static_cast<std::size_t>(node_idx)];
            const double lat = node_lats[static_cast<std::size_t>(node_idx)];
            sum_lon += lon;
            sum_lat += lat;
            lon_bounds.push_back(lon);
            lat_bounds.push_back(lat);
        }

        elem_lons[e] = sum_lon / static_cast<double>(n_nodes);
        elem_lats[e] = sum_lat / static_cast<double>(n_nodes);
    }

    // Construct the SourceGrid
    SourceGrid grid;
    grid.type = SourceGrid::Type::Unstructured;
    grid.num_cells = element_count;
    grid.lats = std::move(elem_lats);
    grid.lons = std::move(elem_lons);
    grid.lat_bounds = std::move(lat_bounds);
    grid.lon_bounds = std::move(lon_bounds);
    grid.metadata_convention = "ESMF_Mesh";
    grid.bounds_inferred = false;

    return grid;
}

auto read_esmf_grid_spec(const std::filesystem::path& path)
    -> std::expected<SourceGrid, Error> {

    // Open the NetCDF file
    NcGuard guard;
    int rc = nc_open(path.c_str(), NC_NOWRITE, &guard.ncid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfFileNotFound),
            std::string("Cannot open ESMF grid spec file: ") + path.string() +
                " (" + nc_strerror(rc) + ")",
            "grid"});
    }

    const int ncid = guard.ncid;

    // Read grid_center_lat variable
    int varid{-1};
    rc = nc_inq_varid(ncid, "grid_center_lat", &varid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfGridSpecMissingVariable),
            "ESMF grid spec file missing variable 'grid_center_lat': " + path.string(),
            "grid"});
    }

    // Determine the size of grid_center_lat (could be 1D or 2D)
    int ndims{0};
    nc_inq_varndims(ncid, varid, &ndims);

    std::size_t grid_size{0};
    if (ndims == 1) {
        int dimids[1]{};
        nc_inq_vardimid(ncid, varid, dimids);
        nc_inq_dimlen(ncid, dimids[0], &grid_size);
    } else if (ndims == 2) {
        // 2D: (nlat, nlon) — compute total size
        int dimids[2]{};
        nc_inq_vardimid(ncid, varid, dimids);
        std::size_t dim0{0}, dim1{0};
        nc_inq_dimlen(ncid, dimids[0], &dim0);
        nc_inq_dimlen(ncid, dimids[1], &dim1);
        grid_size = dim0 * dim1;
    }

    if (grid_size == 0) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfGridSpecMissingVariable),
            "ESMF grid spec file has empty 'grid_center_lat': " + path.string(),
            "grid"});
    }

    std::vector<double> center_lats(grid_size);
    rc = nc_get_var_double(ncid, varid, center_lats.data());
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfGridSpecMissingVariable),
            "Failed to read 'grid_center_lat': " + std::string(nc_strerror(rc)),
            "grid"});
    }

    // Read grid_center_lon variable
    rc = nc_inq_varid(ncid, "grid_center_lon", &varid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfGridSpecMissingVariable),
            "ESMF grid spec file missing variable 'grid_center_lon': " + path.string(),
            "grid"});
    }

    std::vector<double> center_lons(grid_size);
    rc = nc_get_var_double(ncid, varid, center_lons.data());
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::EsmfGridSpecMissingVariable),
            "Failed to read 'grid_center_lon': " + std::string(nc_strerror(rc)),
            "grid"});
    }

    // Try to read optional corner coordinates
    std::vector<double> corner_lats;
    std::vector<double> corner_lons;
    bool has_corners = false;

    int corner_lat_varid{-1};
    int corner_lon_varid{-1};
    if (nc_inq_varid(ncid, "grid_corner_lat", &corner_lat_varid) == NC_NOERR &&
        nc_inq_varid(ncid, "grid_corner_lon", &corner_lon_varid) == NC_NOERR) {

        // Determine corner dimensions: (grid_size, grid_corners)
        int corner_ndims{0};
        nc_inq_varndims(ncid, corner_lat_varid, &corner_ndims);

        std::size_t total_corners{0};
        if (corner_ndims == 2) {
            int dimids[2]{};
            nc_inq_vardimid(ncid, corner_lat_varid, dimids);
            std::size_t dim0{0}, dim1{0};
            nc_inq_dimlen(ncid, dimids[0], &dim0);
            nc_inq_dimlen(ncid, dimids[1], &dim1);
            total_corners = dim0 * dim1;
        } else if (corner_ndims == 1) {
            int dimids[1]{};
            nc_inq_vardimid(ncid, corner_lat_varid, dimids);
            nc_inq_dimlen(ncid, dimids[0], &total_corners);
        }

        if (total_corners > 0) {
            corner_lats.resize(total_corners);
            corner_lons.resize(total_corners);

            rc = nc_get_var_double(ncid, corner_lat_varid, corner_lats.data());
            if (rc == NC_NOERR) {
                rc = nc_get_var_double(ncid, corner_lon_varid, corner_lons.data());
                if (rc == NC_NOERR) {
                    has_corners = true;
                }
            }
        }
    }

    // Construct the SourceGrid
    SourceGrid grid;
    grid.type = SourceGrid::Type::RegularLatLon;
    grid.num_cells = grid_size;
    grid.lats = std::move(center_lats);
    grid.lons = std::move(center_lons);
    grid.metadata_convention = "ESMF_GridSpec";

    if (has_corners) {
        grid.lat_bounds = std::move(corner_lats);
        grid.lon_bounds = std::move(corner_lons);
        grid.bounds_inferred = false;
    } else {
        grid.bounds_inferred = true;
    }

    return grid;
}

auto read_esmf_as_target(const std::filesystem::path& path,
                          EsmfFileType type)
    -> std::expected<TargetGrid, Error> {

    // Read as source grid first to get coordinates
    if (type == EsmfFileType::Mesh) {
        auto source_result = read_esmf_mesh(path);
        if (!source_result) {
            return std::unexpected(source_result.error());
        }

        const auto& source = source_result.value();

        TargetGrid target;
        target.num_cols = source.num_cells;
        target.num_levels = 0;
        target.lats = source.lats;
        target.lons = source.lons;
        target.esmf_mesh = path.string();

        return target;
    } else {
        // GridSpec
        auto source_result = read_esmf_grid_spec(path);
        if (!source_result) {
            return std::unexpected(source_result.error());
        }

        const auto& source = source_result.value();

        TargetGrid target;
        target.num_cols = source.num_cells;
        target.num_levels = 0;
        target.lats = source.lats;
        target.lons = source.lons;
        target.esmf_grid_spec = path.string();

        return target;
    }
}

} // namespace tide::grid
