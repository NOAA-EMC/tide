!> @file test_regrid_error_paths.F90
!> @brief Unit tests for Regrid Manager error paths.
!>
!> Validates Requirements 3.4 and 3.5:
!>   3.4 - Nonexistent target_grid_file returns error with descriptive message
!>   3.5 - Unrecognized file format returns error with descriptive message
program test_regrid_error_paths
  use ESMF
  use tide_regrid_mod
  implicit none

  type(tide_regrid_state_t) :: state
  type(ESMF_Mesh) :: mesh
  integer :: rc, test_rc
  integer :: num_pass, num_fail
  integer :: my_task
  type(ESMF_VM) :: vm
  integer, allocatable :: empty_int(:)
  real(ESMF_KIND_R8), allocatable :: empty_real(:)
  character(len=256) :: invalid_file

  num_pass = 0
  num_fail = 0

  allocate(empty_int(0))
  allocate(empty_real(0))

  ! -----------------------------------------------------------------------
  ! Initialize ESMF
  ! -----------------------------------------------------------------------
  call ESMF_Initialize(rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: ESMF_Initialize failed"
    stop 1
  end if

  call ESMF_VMGetCurrent(vm, rc=rc)
  call ESMF_VMGet(vm, localPet=my_task, rc=rc)

  ! -----------------------------------------------------------------------
  ! Create a minimal source mesh (single quad element on task 0)
  ! -----------------------------------------------------------------------
  mesh = ESMF_MeshCreate(parametricDim=2, spatialDim=2, rc=rc)
  if (rc /= ESMF_SUCCESS) then
    print *, "FATAL: ESMF_MeshCreate failed"
    stop 1
  end if

  if (my_task == 0) then
    call ESMF_MeshAddNodes(mesh, [1,2,3,4], &
         [0.0d0, 0.0d0, 1.0d0, 0.0d0, 1.0d0, 1.0d0, 0.0d0, 1.0d0], &
         [0,0,0,0], rc=rc)
    call ESMF_MeshAddElements(mesh, [1], &
         [ESMF_MESHELEMTYPE_QUAD], [1,2,3,4], rc=rc)
  else
    call ESMF_MeshAddNodes(mesh, empty_int, empty_real, empty_int, rc=rc)
    call ESMF_MeshAddElements(mesh, empty_int, empty_int, empty_int, rc=rc)
  end if

  ! =====================================================================
  ! Test 1: tide_regrid_init with nonexistent file → error
  !   Validates Requirement 3.4
  ! =====================================================================
  call tide_regrid_init(state, mesh, "/no/such/path/nonexistent_grid.nc", &
                        "bilinear", test_rc)
  if (test_rc /= ESMF_SUCCESS) then
    print *, "PASS: Test 1 - nonexistent file returns error"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 1 - nonexistent file should have returned error"
    num_fail = num_fail + 1
  end if

  ! =====================================================================
  ! Test 2: tide_regrid_init with invalid format file → error
  !   Validates Requirement 3.5
  ! =====================================================================
  ! Create a temporary file with invalid (non-grid) content
  invalid_file = "test_invalid_grid.tmp"
  if (my_task == 0) then
    open(unit=77, file=trim(invalid_file), status='replace', &
         form='formatted', iostat=rc)
    write(77, '(A)') "This is not a valid SCRIP or GRIDSPEC file."
    write(77, '(A)') "Just plain text content."
    close(77)
  end if

  ! Barrier so all tasks see the file
  call ESMF_VMBarrier(vm, rc=rc)

  call tide_regrid_init(state, mesh, trim(invalid_file), "bilinear", test_rc)
  if (test_rc /= ESMF_SUCCESS) then
    print *, "PASS: Test 2 - invalid format file returns error"
    num_pass = num_pass + 1
  else
    print *, "FAIL: Test 2 - invalid format file should have returned error"
    num_fail = num_fail + 1
  end if

  ! Clean up temp file
  if (my_task == 0) then
    open(unit=77, file=trim(invalid_file), status='old', iostat=rc)
    if (rc == 0) close(77, status='delete')
  end if

  ! -----------------------------------------------------------------------
  ! Summary
  ! -----------------------------------------------------------------------
  print *, "====================================="
  print *, "Regrid error path tests: ", num_pass, " passed, ", num_fail, " failed"
  print *, "====================================="

  ! -----------------------------------------------------------------------
  ! Cleanup
  ! -----------------------------------------------------------------------
  call ESMF_MeshDestroy(mesh, rc=rc)
  call ESMF_Finalize(rc=rc)

  deallocate(empty_int)
  deallocate(empty_real)

  if (num_fail > 0) stop 1

end program test_regrid_error_paths
