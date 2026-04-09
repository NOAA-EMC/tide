!> @file tide_regrid_mod.F90
!> @brief Regrid Manager module for the TIDE library.
!>
!> Provides target grid loading (SCRIP/GRIDSPEC), ESMF RouteHandle creation,
!> field regridding, and resource cleanup.
module tide_regrid_mod
  use ESMF
  use shr_kind_mod, only : r8 => shr_kind_r8, cl => shr_kind_cl
  implicit none
  private

  ! -------------------------------------------------------------------------
  ! Public types
  ! -------------------------------------------------------------------------

  !> @brief State for a single regrid operation (one per output-enabled stream).
  type, public :: tide_regrid_state_t
    type(ESMF_Grid)        :: target_grid   !< Target grid loaded from file
    type(ESMF_RouteHandle) :: route_handle  !< Precomputed regrid weights
    type(ESMF_Field)       :: src_field     !< Scratch source field on model mesh
    type(ESMF_Field)       :: dst_field     !< Scratch destination field on target grid
    logical                :: initialized = .false.
  end type tide_regrid_state_t

  ! -------------------------------------------------------------------------
  ! Public interface
  ! -------------------------------------------------------------------------
  public :: tide_regrid_init
  public :: tide_regrid_apply
  public :: tide_regrid_finalize

contains

  ! ===========================================================================
  ! Regrid method string to ESMF constant mapping
  ! ===========================================================================

  !> @brief Map a regrid_method string to the corresponding ESMF constant.
  !> @param method_str  One of "bilinear","neareststod","nearestdtos","conserve".
  !> @param esmf_method Output ESMF_RegridMethod_Flag.
  !> @param rc          ESMF_SUCCESS or ESMF_FAILURE if unrecognised.
  subroutine map_regrid_method(method_str, esmf_method, rc)
    character(len=*),              intent(in)  :: method_str
    type(ESMF_RegridMethod_Flag),  intent(out) :: esmf_method
    integer,                       intent(out) :: rc

    rc = ESMF_SUCCESS

    select case (trim(method_str))
      case ("bilinear")
        esmf_method = ESMF_REGRIDMETHOD_BILINEAR
      case ("neareststod")
        esmf_method = ESMF_REGRIDMETHOD_NEAREST_STOD
      case ("nearestdtos")
        esmf_method = ESMF_REGRIDMETHOD_NEAREST_DTOS
      case ("conserve")
        esmf_method = ESMF_REGRIDMETHOD_CONSERVE
      case default
        write(*,*) "ERROR: [TIDE] Unrecognized regrid_method: '", trim(method_str), "'"
        write(*,*) "ERROR: [TIDE] Valid values: bilinear, neareststod, nearestdtos, conserve"
        rc = ESMF_FAILURE
    end select
  end subroutine map_regrid_method

  ! ===========================================================================
  ! Target grid loading (SCRIP then GRIDSPEC fallback)
  ! ===========================================================================

  !> @brief Try to load a target grid from file, attempting SCRIP first then GRIDSPEC.
  !> @param grid_file  Path to the grid description file.
  !> @param grid       Output ESMF_Grid on success.
  !> @param rc         ESMF_SUCCESS or ESMF_FAILURE if both formats fail.
  subroutine load_target_grid(grid_file, grid, rc)
    character(len=*),  intent(in)  :: grid_file
    type(ESMF_Grid),   intent(out) :: grid
    integer,           intent(out) :: rc

    integer :: scrip_rc, gridspec_rc

    rc = ESMF_SUCCESS

    ! Attempt SCRIP format first
    grid = ESMF_GridCreate(filename=trim(grid_file), &
                           fileFormat=ESMF_FILEFORMAT_SCRIP, &
                           addCornerStagger=.true., &
                           rc=scrip_rc)
    if (scrip_rc == ESMF_SUCCESS) then
      write(*,*) "INFO: [TIDE] Loaded target grid from SCRIP file: ", trim(grid_file)
      return
    end if

    ! Fall back to GRIDSPEC format
    write(*,*) "INFO: [TIDE] SCRIP format failed for '", trim(grid_file), &
               "', trying GRIDSPEC..."
    grid = ESMF_GridCreate(filename=trim(grid_file), &
                           fileFormat=ESMF_FILEFORMAT_GRIDSPEC, &
                           addCornerStagger=.true., &
                           rc=gridspec_rc)
    if (gridspec_rc == ESMF_SUCCESS) then
      write(*,*) "INFO: [TIDE] Loaded target grid from GRIDSPEC file: ", trim(grid_file)
      return
    end if

    ! Both formats failed
    write(*,*) "ERROR: [TIDE] Failed to load target grid from file: ", trim(grid_file)
    write(*,*) "ERROR: [TIDE] Attempted SCRIP (rc=", scrip_rc, &
               ") and GRIDSPEC (rc=", gridspec_rc, ")"
    rc = ESMF_FAILURE
  end subroutine load_target_grid

  ! ===========================================================================
  ! Grid info logging
  ! ===========================================================================

  !> @brief Log target grid dimensions and coordinate range.
  !> @param grid  The ESMF_Grid to inspect.
  subroutine log_grid_info(grid)
    type(ESMF_Grid), intent(in) :: grid

    integer :: dimCount, tileCount, grid_rc
    integer :: lbnd(2), ubnd(2)
    real(ESMF_KIND_R8), pointer :: coordX(:,:), coordY(:,:)
    real(ESMF_KIND_R8) :: minX, maxX, minY, maxY

    call ESMF_GridGet(grid, dimCount=dimCount, tileCount=tileCount, rc=grid_rc)
    if (grid_rc /= ESMF_SUCCESS) then
      write(*,*) "WARNING: [TIDE] Could not query target grid dimensions"
      return
    end if

    write(*,*) "INFO: [TIDE] Target grid: dimCount=", dimCount, " tileCount=", tileCount

    ! Try to get coordinate range from the center stagger
    nullify(coordX, coordY)
    call ESMF_GridGetCoord(grid, coordDim=1, &
                           computationalLBound=lbnd, computationalUBound=ubnd, &
                           farrayPtr=coordX, rc=grid_rc)
    if (grid_rc == ESMF_SUCCESS .and. associated(coordX)) then
      minX = minval(coordX)
      maxX = maxval(coordX)
    else
      minX = 0.0_ESMF_KIND_R8
      maxX = 0.0_ESMF_KIND_R8
    end if

    call ESMF_GridGetCoord(grid, coordDim=2, &
                           farrayPtr=coordY, rc=grid_rc)
    if (grid_rc == ESMF_SUCCESS .and. associated(coordY)) then
      minY = minval(coordY)
      maxY = maxval(coordY)
    else
      minY = 0.0_ESMF_KIND_R8
      maxY = 0.0_ESMF_KIND_R8
    end if

    write(*,*) "INFO: [TIDE] Target grid size: ", &
               (ubnd(1) - lbnd(1) + 1), " x ", (ubnd(2) - lbnd(2) + 1)
    write(*,*) "INFO: [TIDE] Coordinate range: lon=[", minX, ",", maxX, &
               "] lat=[", minY, ",", maxY, "]"
  end subroutine log_grid_info

  ! ===========================================================================
  ! Initialisation
  ! ===========================================================================

  !> @brief Initialise regrid state: load target grid, create RouteHandle.
  !>
  !> Loads the target grid from a SCRIP or GRIDSPEC file, creates scratch
  !> source/destination ESMF Fields, and precomputes the RouteHandle for the
  !> requested regrid method.
  !>
  !> @param state            Regrid state to initialise.
  !> @param source_mesh      ESMF Mesh of the host model (source geometry).
  !> @param target_grid_file Path to the SCRIP or GRIDSPEC grid file.
  !> @param regrid_method    One of "bilinear","neareststod","nearestdtos","conserve".
  !> @param rc               ESMF_SUCCESS or ESMF_FAILURE.
  subroutine tide_regrid_init(state, source_mesh, target_grid_file, regrid_method, rc)
    type(tide_regrid_state_t), intent(inout) :: state
    type(ESMF_Mesh),           intent(in)    :: source_mesh
    character(len=*),          intent(in)    :: target_grid_file
    character(len=*),          intent(in)    :: regrid_method
    integer,                   intent(out)   :: rc

    type(ESMF_RegridMethod_Flag) :: esmf_method
    integer :: local_rc

    rc = ESMF_SUCCESS
    state%initialized = .false.

    ! --- Map regrid method string to ESMF constant ---
    call map_regrid_method(regrid_method, esmf_method, local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      rc = ESMF_FAILURE
      return
    end if

    ! --- Load target grid (SCRIP first, then GRIDSPEC) ---
    call load_target_grid(target_grid_file, state%target_grid, local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      rc = ESMF_FAILURE
      return
    end if

    ! --- Log grid dimensions and coordinate range ---
    call log_grid_info(state%target_grid)

    ! --- Create scratch source field on the model mesh ---
    state%src_field = ESMF_FieldCreate(source_mesh, typekind=ESMF_TYPEKIND_R8, &
                                       name="tide_regrid_src", rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to create source field on model mesh"
      rc = ESMF_FAILURE
      return
    end if

    ! --- Create scratch destination field on the target grid ---
    state%dst_field = ESMF_FieldCreate(state%target_grid, typekind=ESMF_TYPEKIND_R8, &
                                       name="tide_regrid_dst", rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to create destination field on target grid"
      rc = ESMF_FAILURE
      return
    end if

    ! --- Create RouteHandle via ESMF_FieldRegridStore ---
    call ESMF_FieldRegridStore(srcField=state%src_field, &
                               dstField=state%dst_field, &
                               regridmethod=esmf_method, &
                               routehandle=state%route_handle, &
                               rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to create RouteHandle for regrid method '", &
                 trim(regrid_method), "'"
      write(*,*) "ERROR: [TIDE] Source mesh and target grid may be incompatible"
      rc = ESMF_FAILURE
      return
    end if

    state%initialized = .true.
    write(*,*) "INFO: [TIDE] Regrid initialised: method=", trim(regrid_method), &
               " grid_file=", trim(target_grid_file)

  end subroutine tide_regrid_init

  ! ===========================================================================
  ! Apply regrid
  ! ===========================================================================

  !> @brief Apply the precomputed RouteHandle to regrid one field.
  !>
  !> Copies src_data into the scratch source field, calls ESMF_FieldRegrid,
  !> and copies the result from the scratch destination field into dst_data.
  !>
  !> @param state    Initialised regrid state with a valid RouteHandle.
  !> @param src_data Input data array on the source mesh.
  !> @param dst_data Output data array on the target grid (filled on return).
  !> @param rc       ESMF_SUCCESS or ESMF_FAILURE.
  subroutine tide_regrid_apply(state, src_data, dst_data, rc)
    type(tide_regrid_state_t), intent(inout) :: state
    real(ESMF_KIND_R8),        intent(in)    :: src_data(:)
    real(ESMF_KIND_R8),        intent(inout) :: dst_data(:)
    integer,                   intent(out)   :: rc

    real(ESMF_KIND_R8), pointer :: fptr(:)
    integer :: local_rc

    rc = ESMF_SUCCESS

    if (.not. state%initialized) then
      write(*,*) "ERROR: [TIDE] tide_regrid_apply called on uninitialised state"
      rc = ESMF_FAILURE
      return
    end if

    ! --- Copy src_data into the scratch source field ---
    nullify(fptr)
    call ESMF_FieldGet(state%src_field, farrayPtr=fptr, rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to get pointer to source field"
      rc = ESMF_FAILURE
      return
    end if
    fptr(:) = src_data(:)

    ! --- Apply the precomputed RouteHandle ---
    call ESMF_FieldRegrid(state%src_field, state%dst_field, &
                          routehandle=state%route_handle, rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] ESMF_FieldRegrid failed"
      rc = ESMF_FAILURE
      return
    end if

    ! --- Copy result from the scratch destination field into dst_data ---
    nullify(fptr)
    call ESMF_FieldGet(state%dst_field, farrayPtr=fptr, rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,*) "ERROR: [TIDE] Failed to get pointer to destination field"
      rc = ESMF_FAILURE
      return
    end if
    dst_data(:) = fptr(:)

  end subroutine tide_regrid_apply

  ! ===========================================================================
  ! Finalisation
  ! ===========================================================================

  !> @brief Destroy all ESMF objects held by the regrid state.
  !>
  !> Releases the RouteHandle, source/destination fields, and target grid.
  !> Sets state%initialized to .false.
  !>
  !> @param state  Regrid state to finalise.
  !> @param rc     ESMF_SUCCESS or ESMF_FAILURE.
  subroutine tide_regrid_finalize(state, rc)
    type(tide_regrid_state_t), intent(inout) :: state
    integer,                   intent(out)   :: rc

    integer :: local_rc

    rc = ESMF_SUCCESS

    if (.not. state%initialized) then
      write(*,*) "WARNING: [TIDE] tide_regrid_finalize called on uninitialised state, skipping"
      return
    end if

    ! --- Destroy RouteHandle ---
    call ESMF_RouteHandleDestroy(state%route_handle, rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,*) "WARNING: [TIDE] Failed to destroy RouteHandle"
      rc = ESMF_FAILURE
    end if

    ! --- Destroy source field ---
    call ESMF_FieldDestroy(state%src_field, rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,*) "WARNING: [TIDE] Failed to destroy source field"
      rc = ESMF_FAILURE
    end if

    ! --- Destroy destination field ---
    call ESMF_FieldDestroy(state%dst_field, rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,*) "WARNING: [TIDE] Failed to destroy destination field"
      rc = ESMF_FAILURE
    end if

    ! --- Destroy target grid ---
    call ESMF_GridDestroy(state%target_grid, rc=local_rc)
    if (local_rc /= ESMF_SUCCESS) then
      write(*,*) "WARNING: [TIDE] Failed to destroy target grid"
      rc = ESMF_FAILURE
    end if

    state%initialized = .false.
    write(*,*) "INFO: [TIDE] Regrid state finalised"

  end subroutine tide_regrid_finalize

end module tide_regrid_mod
