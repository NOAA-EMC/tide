!> @file driver_example.f90
!> @brief Example Fortran driver demonstrating TIDE integration into a model
!>        timestep loop.
!>
!> This program shows the full TIDE lifecycle:
!>   1. Query stream count from a YAML configuration
!>   2. Initialize multiple forcing streams via tide_init_multi
!>   3. Run a timestep loop advancing all streams and retrieving fields
!>   4. Print summary statistics (min, max, mean) at each timestep
!>   5. Finalize all streams and release resources
!>
!> Build:
!>   cmake -S . -B build -DTIDE_BUILD_EXAMPLES=ON
!>   cmake --build build
!>
!> Run:
!>   mpirun -np 1 ./build/examples/driver_example <config.yaml>
!>
!> Validates: Requirement 10.6
program driver_example
  use, intrinsic :: iso_c_binding
  use tide_mod
  implicit none
  include 'mpif.h'

  ! ---------------------------------------------------------------------------
  ! Configuration parameters
  ! ---------------------------------------------------------------------------
  integer, parameter :: MAX_STREAMS = 64   ! Maximum streams supported
  integer, parameter :: NUM_TIMESTEPS = 10 ! Number of timesteps to advance
  real(c_double), parameter :: DT = 3600.0d0  ! Timestep size in seconds (1 hour)

  ! ---------------------------------------------------------------------------
  ! Local variables
  ! ---------------------------------------------------------------------------
  type(c_ptr)        :: handles(MAX_STREAMS)
  integer            :: num_streams
  integer            :: status_codes(MAX_STREAMS)
  integer            :: ierr, mpi_ierr
  integer            :: istep, istream
  real(c_double)     :: current_time
  real(c_double)     :: timers(5)
  character(len=512) :: config_path
  character(len=256) :: errmsg
  integer            :: msg_len

  ! Field retrieval variables
  type(c_ptr)        :: data_ptr
  integer            :: rank
  integer(c_size_t)  :: extents(7)
  integer            :: total_elements
  real(c_double), pointer :: field(:)
  real(c_double)     :: fmin, fmax, fmean

  ! ---------------------------------------------------------------------------
  ! Step 0: Initialize MPI
  ! MPI is required because TIDE uses MPI-parallel I/O internally via AMIO.
  ! ---------------------------------------------------------------------------
  call MPI_Init(mpi_ierr)
  if (mpi_ierr /= 0) then
    write(*,'(A)') 'FATAL: MPI_Init failed'
    stop 1
  end if

  ! ---------------------------------------------------------------------------
  ! Step 1: Parse command-line argument for the config file path
  ! The YAML configuration specifies all stream definitions, grid files,
  ! weight files, and optional settings (memory budget, prefetch, timers).
  ! ---------------------------------------------------------------------------
  if (command_argument_count() < 1) then
    write(*,'(A)') 'Usage: driver_example <config.yaml>'
    write(*,'(A)') ''
    write(*,'(A)') 'The YAML configuration file defines forcing streams.'
    write(*,'(A)') 'See the TIDE documentation for the configuration schema.'
    call MPI_Finalize(mpi_ierr)
    stop 1
  end if
  call get_command_argument(1, config_path)

  write(*,'(A)')    '============================================='
  write(*,'(A)')    '  TIDE Fortran Driver Example'
  write(*,'(A)')    '============================================='
  write(*,'(A,A)')  '  Config file : ', trim(config_path)
  write(*,'(A,I0)') '  Timesteps   : ', NUM_TIMESTEPS
  write(*,'(A,F8.1,A)') '  DT          : ', DT, ' seconds'
  write(*,'(A)')    '============================================='
  write(*,*)

  ! ---------------------------------------------------------------------------
  ! Step 2: Query how many streams are defined in the configuration
  ! This allows us to verify the config is readable and know what to expect
  ! from the initialization step.
  ! ---------------------------------------------------------------------------
  call tide_get_stream_count(config_path, num_streams, ierr)
  if (ierr /= 0) then
    write(*,'(A,I0)') 'ERROR: tide_get_stream_count failed with code ', ierr
    call MPI_Finalize(mpi_ierr)
    stop 1
  end if
  write(*,'(A,I0)') 'Configured streams: ', num_streams

  ! Safety check: ensure we have room in our handles array
  if (num_streams > MAX_STREAMS) then
    write(*,'(A,I0,A,I0)') 'ERROR: config has ', num_streams, &
                            ' streams but MAX_STREAMS = ', MAX_STREAMS
    call MPI_Finalize(mpi_ierr)
    stop 1
  end if

  ! ---------------------------------------------------------------------------
  ! Step 3: Initialize all streams via tide_init_multi
  ! This reads grid files, weight files, and opens forcing data files.
  ! On error, print the diagnostic message and exit cleanly.
  ! ---------------------------------------------------------------------------
  write(*,'(A)') 'Initializing TIDE streams...'
  call tide_init_multi(config_path, MPI_COMM_WORLD, handles, &
                       num_streams, MAX_STREAMS, ierr)
  if (ierr /= 0) then
    write(*,'(A,I0)') 'ERROR: tide_init_multi failed with code ', ierr
    ! Attempt to get the error message for diagnostics
    call tide_get_error(handles(1), errmsg, msg_len)
    if (msg_len > 0) then
      write(*,'(A,A)') '  Detail: ', trim(errmsg)
    end if
    call MPI_Finalize(mpi_ierr)
    stop 1
  end if
  write(*,'(A,I0,A)') 'Successfully initialized ', num_streams, ' stream(s).'
  write(*,*)

  ! ---------------------------------------------------------------------------
  ! Step 4: Timestep loop — advance all streams and retrieve fields
  ! This is the core of model integration. Each iteration:
  !   a) Advance all streams to the current simulation time
  !   b) Check per-stream status codes for failures
  !   c) Retrieve each stream's field and compute summary statistics
  ! ---------------------------------------------------------------------------
  current_time = 0.0d0

  do istep = 1, NUM_TIMESTEPS
    current_time = current_time + DT

    write(*,'(A,I3,A,F10.1,A)') '--- Timestep ', istep, &
                                 '  t = ', current_time, ' s ---'

    ! Advance all streams to the current time. Per-stream errors are
    ! isolated: a failure in one stream does not halt the others.
    call tide_advance_all(handles, num_streams, current_time, &
                          status_codes, ierr)

    ! Check overall status. ierr=1 means at least one stream failed.
    if (ierr /= 0) then
      write(*,'(A)') '  WARNING: one or more streams failed during advance'
    end if

    ! Retrieve and summarize each stream's output field
    do istream = 1, num_streams
      if (status_codes(istream) /= 0) then
        ! This stream failed — report the error and continue with others
        write(*,'(A,I0,A,I0)') '  Stream ', istream, &
                                ' FAILED with code ', status_codes(istream)
        cycle
      end if

      ! Retrieve the field data pointer and dimension metadata.
      ! The field name used here must match what's in the YAML config.
      ! For a generic driver, we retrieve whatever field name each stream
      ! produces. Here we use a placeholder call pattern; in practice the
      ! caller knows the field names from the config.
      extents = 0
      call tide_get_field(handles(istream), "field", data_ptr, &
                          rank, extents, ierr)
      if (ierr /= 0) then
        write(*,'(A,I0,A,I0)') '  Stream ', istream, &
                                ': tide_get_field error ', ierr
        cycle
      end if

      ! Compute total element count from returned extents
      total_elements = 1
      do msg_len = 1, rank
        total_elements = total_elements * int(extents(msg_len))
      end do

      ! Associate C pointer with Fortran array for direct access
      if (c_associated(data_ptr) .and. total_elements > 0) then
        call c_f_pointer(data_ptr, field, [total_elements])

        ! Compute min, max, mean summary statistics
        fmin = minval(field(1:total_elements))
        fmax = maxval(field(1:total_elements))
        fmean = sum(field(1:total_elements)) / real(total_elements, c_double)

        write(*,'(A,I0,A,I0,A,ES12.5,A,ES12.5,A,ES12.5)') &
          '  Stream ', istream, ' (n=', total_elements, &
          '): min=', fmin, ' max=', fmax, ' mean=', fmean
      else
        write(*,'(A,I0,A)') '  Stream ', istream, ': no data available'
      end if
    end do

    write(*,*)
  end do

  ! ---------------------------------------------------------------------------
  ! Step 5: Report performance timers
  ! After the timestep loop, retrieve cumulative per-stage timing to
  ! identify performance bottlenecks.
  ! ---------------------------------------------------------------------------
  write(*,'(A)') '--- Performance Timers (cumulative seconds) ---'
  write(*,'(A)') '  Stream   I/O      Temporal Spatial  Vertical Scaling'

  do istream = 1, num_streams
    call tide_get_timers(handles(istream), timers, ierr)
    if (ierr == 0) then
      write(*,'(A,I3,5(2X,F8.4))') '  ', istream, &
        timers(1), timers(2), timers(3), timers(4), timers(5)
    end if
  end do
  write(*,*)

  ! ---------------------------------------------------------------------------
  ! Step 6: Finalize all streams and release resources
  ! This closes file handles, frees internal buffers and cached weights.
  ! After this call, handles must not be reused.
  ! ---------------------------------------------------------------------------
  write(*,'(A)') 'Finalizing TIDE streams...'
  call tide_finalize_all(handles, num_streams, ierr)
  if (ierr /= 0) then
    write(*,'(A,I0)') 'WARNING: tide_finalize_all returned code ', ierr
  else
    write(*,'(A)') 'All streams finalized successfully.'
  end if

  ! ---------------------------------------------------------------------------
  ! Step 7: Finalize MPI
  ! ---------------------------------------------------------------------------
  call MPI_Finalize(mpi_ierr)

  write(*,*)
  write(*,'(A)') 'TIDE driver example completed.'

end program driver_example
