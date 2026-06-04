!> @file tide_mod.f90
!> @brief TIDE Fortran binding module using ISO_C_BINDING.
!>
!> Provides explicit Fortran interface declarations wrapping every function
!> in the C API header (tide.h). Includes convenience subroutines that
!> handle null-termination of strings and type conversion so that Fortran
!> callers interact with TIDE using natural Fortran types.
!>
!> Memory ownership: TIDE retains ownership of all field data pointers.
!> The Fortran caller SHALL NOT deallocate returned C pointers. Data
!> remains valid from return until the next call to tide_advance().
!>
!> Usage:
!>   use tide_mod
!>   type(c_ptr) :: handle
!>   integer :: ierr
!>   call tide_init("config.yaml", mpi_comm, handle, ierr)
!>   call tide_advance(handle, target_time, ierr)
!>   ...
!>   call tide_finalize(handle, ierr)
!>
!> Validates Requirements: 12.3, 12.4, 12.5, 6.5
module tide_mod
  use, intrinsic :: iso_c_binding
  implicit none
  private

  ! Public convenience subroutines — single-stream lifecycle
  public :: tide_init
  public :: tide_advance
  public :: tide_get_field
  public :: tide_finalize
  public :: tide_get_error

  ! Public convenience subroutines — multi-stream management
  ! Validates Requirements: 4.4
  public :: tide_init_multi
  public :: tide_advance_all
  public :: tide_finalize_all
  public :: tide_get_stream_count
  public :: tide_get_stream_status
  public :: tide_get_memory_usage

  ! Public convenience subroutines — performance instrumentation
  ! Validates Requirements: 7.6
  public :: tide_get_timers
  public :: tide_reset_timers

  ! Public convenience subroutines — weight export
  ! Validates Requirements: 9.5
  public :: tide_export_weights

  ! ─────────────────────────────────────────────────────────────────────────
  ! C API interface declarations (bind(C) wrappers for tide.h functions)
  ! ─────────────────────────────────────────────────────────────────────────
  interface

    !> @brief C binding for tide_init
    !> @param config_path Null-terminated path to YAML configuration
    !> @param mpi_comm MPI communicator as Fortran integer
    !> @param handle Receives opaque stream handle on success
    !> @return Integer status code (0 = success)
    function tide_init_c(config_path, mpi_comm, handle) &
        bind(C, name="tide_init") result(ierr)
      import :: c_char, c_int, c_ptr
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value, intent(in) :: mpi_comm
      type(c_ptr), intent(out) :: handle
      integer(c_int) :: ierr
    end function tide_init_c

    !> @brief C binding for tide_advance
    !> @param handle Opaque stream handle from tide_init
    !> @param target_time Simulation time in seconds since epoch
    !> @return Integer status code (0 = success)
    function tide_advance_c(handle, target_time) &
        bind(C, name="tide_advance") result(ierr)
      import :: c_ptr, c_double, c_int
      type(c_ptr), value, intent(in) :: handle
      real(c_double), value, intent(in) :: target_time
      integer(c_int) :: ierr
    end function tide_advance_c

    !> @brief C binding for tide_get_field
    !> @param handle Opaque stream handle
    !> @param field_name Null-terminated field name
    !> @param data_ptr Receives pointer to field data (owned by TIDE)
    !> @param rank Receives dimensionality (1-7)
    !> @param extents Array of 7 elements filled with dimension sizes
    !> @return Integer status code (0 = success)
    function tide_get_field_c(handle, field_name, data_ptr, rank, extents) &
        bind(C, name="tide_get_field") result(ierr)
      import :: c_ptr, c_char, c_int, c_size_t
      type(c_ptr), value, intent(in) :: handle
      character(kind=c_char), intent(in) :: field_name(*)
      type(c_ptr), intent(out) :: data_ptr
      integer(c_int), intent(out) :: rank
      integer(c_size_t), intent(out) :: extents(7)
      integer(c_int) :: ierr
    end function tide_get_field_c

    !> @brief C binding for tide_finalize
    !> @param handle Opaque stream handle to release
    !> @return Integer status code (0 = success)
    function tide_finalize_c(handle) &
        bind(C, name="tide_finalize") result(ierr)
      import :: c_ptr, c_int
      type(c_ptr), value, intent(in) :: handle
      integer(c_int) :: ierr
    end function tide_finalize_c

    !> @brief C binding for tide_get_error
    !> @param handle Stream handle (or C_NULL_PTR for global errors)
    !> @return Pointer to null-terminated error string
    function tide_get_error_c(handle) &
        bind(C, name="tide_get_error") result(msg_ptr)
      import :: c_ptr
      type(c_ptr), value, intent(in) :: handle
      type(c_ptr) :: msg_ptr
    end function tide_get_error_c

    ! ─────────────────────────────────────────────────────────────────────────
    ! Multi-Stream Management C API
    ! Validates Requirements: 4.4
    ! ─────────────────────────────────────────────────────────────────────────

    !> @brief C binding for tide_init_multi
    !> @param config_path Null-terminated path to YAML configuration
    !> @param mpi_comm MPI communicator as Fortran integer
    !> @param handles Caller-allocated array receiving per-stream handles
    !> @param num_streams Receives number of streams initialized
    !> @param max_streams Maximum capacity of handles array
    !> @return Integer status code (0 = success)
    function tide_init_multi_c(config_path, mpi_comm, handles, num_streams, &
        max_streams) bind(C, name="tide_init_multi") result(ierr)
      import :: c_char, c_int, c_ptr
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), value, intent(in) :: mpi_comm
      type(c_ptr), intent(out) :: handles(*)
      integer(c_int), intent(out) :: num_streams
      integer(c_int), value, intent(in) :: max_streams
      integer(c_int) :: ierr
    end function tide_init_multi_c

    !> @brief C binding for tide_advance_all
    !> @param handles Array of stream handles
    !> @param num_streams Number of handles in the array
    !> @param target_time Target simulation time in seconds since epoch
    !> @param status_codes Per-stream return codes (caller-allocated)
    !> @return 0 if all succeeded, 1 if any stream failed
    function tide_advance_all_c(handles, num_streams, target_time, &
        status_codes) bind(C, name="tide_advance_all") result(ierr)
      import :: c_ptr, c_int, c_double
      type(c_ptr), intent(in) :: handles(*)
      integer(c_int), value, intent(in) :: num_streams
      real(c_double), value, intent(in) :: target_time
      integer(c_int), intent(out) :: status_codes(*)
      integer(c_int) :: ierr
    end function tide_advance_all_c

    !> @brief C binding for tide_finalize_all
    !> @param handles Array of stream handles
    !> @param num_streams Number of handles in the array
    !> @return Integer status code (0 = success)
    function tide_finalize_all_c(handles, num_streams) &
        bind(C, name="tide_finalize_all") result(ierr)
      import :: c_ptr, c_int
      type(c_ptr), intent(in) :: handles(*)
      integer(c_int), value, intent(in) :: num_streams
      integer(c_int) :: ierr
    end function tide_finalize_all_c

    !> @brief C binding for tide_get_stream_count
    !> @param config_path Null-terminated path to YAML configuration
    !> @param count Receives the number of configured streams
    !> @return Integer status code (0 = success)
    function tide_get_stream_count_c(config_path, count) &
        bind(C, name="tide_get_stream_count") result(ierr)
      import :: c_char, c_int
      character(kind=c_char), intent(in) :: config_path(*)
      integer(c_int), intent(out) :: count
      integer(c_int) :: ierr
    end function tide_get_stream_count_c

    !> @brief C binding for tide_get_stream_status
    !> @param handle Stream handle
    !> @return 0 = healthy, non-zero = error code from last failure
    function tide_get_stream_status_c(handle) &
        bind(C, name="tide_get_stream_status") result(status)
      import :: c_ptr, c_int
      type(c_ptr), value, intent(in) :: handle
      integer(c_int) :: status
    end function tide_get_stream_status_c

    !> @brief C binding for tide_get_memory_usage
    !> @param handles Array of stream handles
    !> @param num_streams Number of handles in the array
    !> @return Total bytes allocated for pipeline buffers
    function tide_get_memory_usage_c(handles, num_streams) &
        bind(C, name="tide_get_memory_usage") result(nbytes)
      import :: c_ptr, c_int, c_size_t
      type(c_ptr), intent(in) :: handles(*)
      integer(c_int), value, intent(in) :: num_streams
      integer(c_size_t) :: nbytes
    end function tide_get_memory_usage_c

    ! ─────────────────────────────────────────────────────────────────────────
    ! Performance Instrumentation C API
    ! Validates Requirements: 7.6
    ! ─────────────────────────────────────────────────────────────────────────

    !> @brief C binding for tide_get_timers
    !> @param handle Stream handle
    !> @param timers Array of 5 doubles: IO, temporal, spatial, vertical, scaling
    !> @return Integer status code (0 = success)
    function tide_get_timers_c(handle, timers) &
        bind(C, name="tide_get_timers") result(ierr)
      import :: c_ptr, c_double, c_int
      type(c_ptr), value, intent(in) :: handle
      real(c_double), intent(out) :: timers(5)
      integer(c_int) :: ierr
    end function tide_get_timers_c

    !> @brief C binding for tide_reset_timers
    !> @param handle Stream handle
    !> @return Integer status code (0 = success)
    function tide_reset_timers_c(handle) &
        bind(C, name="tide_reset_timers") result(ierr)
      import :: c_ptr, c_int
      type(c_ptr), value, intent(in) :: handle
      integer(c_int) :: ierr
    end function tide_reset_timers_c

    ! ─────────────────────────────────────────────────────────────────────────
    ! Weight Export C API
    ! Validates Requirements: 9.5
    ! ─────────────────────────────────────────────────────────────────────────

    !> @brief C binding for tide_export_weights
    !> @param handle Stream handle (must have weights computed or loaded)
    !> @param output_path Null-terminated path for the output SCRIP NetCDF file
    !> @return Integer status code (0 = success)
    function tide_export_weights_c(handle, output_path) &
        bind(C, name="tide_export_weights") result(ierr)
      import :: c_ptr, c_char, c_int
      type(c_ptr), value, intent(in) :: handle
      character(kind=c_char), intent(in) :: output_path(*)
      integer(c_int) :: ierr
    end function tide_export_weights_c

  end interface

contains

  ! ─────────────────────────────────────────────────────────────────────────
  ! Convenience subroutines
  ! ─────────────────────────────────────────────────────────────────────────

  !> @brief Initialize TIDE from a YAML configuration file.
  !>
  !> Parses configuration, initializes the pipeline, and returns an opaque
  !> handle for subsequent advance/get_field/finalize calls.
  !>
  !> @param[in]  config_path  Path to YAML configuration file
  !> @param[in]  mpi_comm     MPI communicator (Fortran integer)
  !> @param[out] handle       Opaque stream handle (C pointer)
  !> @param[out] ierr         Status code: 0 = success, non-zero = error
  subroutine tide_init(config_path, mpi_comm, handle, ierr)
    character(len=*), intent(in) :: config_path
    integer, intent(in) :: mpi_comm
    type(c_ptr), intent(out) :: handle
    integer, intent(out) :: ierr

    ierr = tide_init_c(trim(config_path) // c_null_char, &
                       int(mpi_comm, c_int), handle)
  end subroutine tide_init

  !> @brief Advance the pipeline to a target simulation time.
  !>
  !> Executes the full five-stage pipeline (read, temporal interp, spatial
  !> regrid, vertical interp, scaling). After success, fields are available
  !> via tide_get_field. Previously returned field pointers are invalidated.
  !>
  !> @param[in]  handle       Opaque stream handle from tide_init
  !> @param[in]  target_time  Simulation time in seconds since epoch
  !> @param[out] ierr         Status code: 0 = success, non-zero = error
  subroutine tide_advance(handle, target_time, ierr)
    type(c_ptr), intent(in) :: handle
    real(c_double), intent(in) :: target_time
    integer, intent(out) :: ierr

    ierr = tide_advance_c(handle, target_time)
  end subroutine tide_advance

  !> @brief Retrieve a computed field as a C pointer with dimension metadata.
  !>
  !> Returns a C pointer to the field data along with rank and per-dimension
  !> extents. The caller can use c_f_pointer to associate the C pointer with
  !> a Fortran array of the appropriate shape.
  !>
  !> Memory ownership: TIDE retains ownership. Do NOT deallocate data_ptr.
  !> Data remains valid until the next call to tide_advance().
  !>
  !> Example usage:
  !>   type(c_ptr) :: fptr
  !>   integer :: rank
  !>   integer(c_size_t) :: extents(7)
  !>   real(c_double), pointer :: field(:,:,:)
  !>   call tide_get_field(handle, "temperature", fptr, rank, extents, ierr)
  !>   if (ierr == 0) then
  !>     call c_f_pointer(fptr, field, [int(extents(1)), int(extents(2)), int(extents(3))])
  !>   end if
  !>
  !> @param[in]  handle      Opaque stream handle
  !> @param[in]  field_name  Name of the field to retrieve
  !> @param[out] data_ptr    C pointer to field data (owned by TIDE)
  !> @param[out] rank        Dimensionality of the field (1-7)
  !> @param[out] extents     Array of 7 elements with dimension sizes
  !> @param[out] ierr        Status code: 0 = success, non-zero = error
  subroutine tide_get_field(handle, field_name, data_ptr, rank, extents, ierr)
    type(c_ptr), intent(in) :: handle
    character(len=*), intent(in) :: field_name
    type(c_ptr), intent(out) :: data_ptr
    integer, intent(out) :: rank
    integer(c_size_t), intent(out) :: extents(7)
    integer, intent(out) :: ierr

    ierr = tide_get_field_c(handle, trim(field_name) // c_null_char, &
                            data_ptr, rank, extents)
  end subroutine tide_get_field

  !> @brief Release all resources associated with a stream handle.
  !>
  !> Closes file handles, deallocates internal buffers, and invalidates
  !> the handle. After this call, the handle must not be used again.
  !>
  !> @param[in]  handle  Opaque stream handle to finalize
  !> @param[out] ierr    Status code: 0 = success, non-zero = error
  subroutine tide_finalize(handle, ierr)
    type(c_ptr), intent(in) :: handle
    integer, intent(out) :: ierr

    ierr = tide_finalize_c(handle)
  end subroutine tide_finalize

  !> @brief Retrieve the last error message for a stream handle.
  !>
  !> Copies the null-terminated C error string into a Fortran character
  !> variable. Pass C_NULL_PTR as handle to retrieve global errors
  !> (e.g., from a failed tide_init call).
  !>
  !> @param[in]  handle   Stream handle (or C_NULL_PTR for global errors)
  !> @param[out] message  Fortran character variable to receive the message
  !> @param[out] msg_len  Actual length of the error message (0 if no error)
  subroutine tide_get_error(handle, message, msg_len)
    type(c_ptr), intent(in) :: handle
    character(len=*), intent(out) :: message
    integer, intent(out) :: msg_len

    type(c_ptr) :: cstr_ptr
    character(kind=c_char), pointer :: cstr(:)
    integer :: i, max_len

    message = ' '
    msg_len = 0

    cstr_ptr = tide_get_error_c(handle)
    if (.not. c_associated(cstr_ptr)) return

    max_len = len(message)
    call c_f_pointer(cstr_ptr, cstr, [max_len])

    do i = 1, max_len
      if (cstr(i) == c_null_char) exit
      message(i:i) = cstr(i)
    end do
    msg_len = i - 1

  end subroutine tide_get_error

  ! ─────────────────────────────────────────────────────────────────────────
  ! Multi-Stream Management convenience subroutines
  ! Validates Requirements: 4.4
  ! ─────────────────────────────────────────────────────────────────────────

  !> @brief Initialize all streams from a YAML configuration file.
  !>
  !> Parses configuration, creates a StreamManager, and returns an array
  !> of per-stream handles. Each handle can also be used with the single-
  !> stream API (tide_advance, tide_get_field) for individual operations.
  !>
  !> @param[in]  config_path  Path to YAML configuration file
  !> @param[in]  mpi_comm     MPI communicator (Fortran integer)
  !> @param[out] handles      Array of stream handles (caller-allocated)
  !> @param[out] num_streams  Number of streams actually initialized
  !> @param[in]  max_streams  Maximum capacity of handles array
  !> @param[out] ierr         Status code: 0 = success, non-zero = error
  subroutine tide_init_multi(config_path, mpi_comm, handles, num_streams, &
      max_streams, ierr)
    character(len=*), intent(in) :: config_path
    integer, intent(in) :: mpi_comm
    type(c_ptr), intent(out) :: handles(*)
    integer, intent(out) :: num_streams
    integer, intent(in) :: max_streams
    integer, intent(out) :: ierr

    integer(c_int) :: c_num_streams

    ierr = tide_init_multi_c(trim(config_path) // c_null_char, &
                             int(mpi_comm, c_int), handles, &
                             c_num_streams, int(max_streams, c_int))
    num_streams = int(c_num_streams)
  end subroutine tide_init_multi

  !> @brief Advance all managed streams to a target simulation time.
  !>
  !> Iterates over the array of stream handles, advancing each to the
  !> target time. Per-stream errors are isolated: a failure in one stream
  !> does not prevent other streams from advancing.
  !>
  !> @param[in]  handles       Array of stream handles from tide_init_multi
  !> @param[in]  num_streams   Number of handles in the array
  !> @param[in]  target_time   Target simulation time (seconds since epoch)
  !> @param[out] status_codes  Per-stream return codes (0 = success)
  !> @param[out] ierr          0 if all succeeded, 1 if any stream failed
  subroutine tide_advance_all(handles, num_streams, target_time, &
      status_codes, ierr)
    type(c_ptr), intent(in) :: handles(*)
    integer, intent(in) :: num_streams
    real(c_double), intent(in) :: target_time
    integer, intent(out) :: status_codes(*)
    integer, intent(out) :: ierr

    ierr = tide_advance_all_c(handles, int(num_streams, c_int), &
                              target_time, status_codes)
  end subroutine tide_advance_all

  !> @brief Finalize all managed streams and release their resources.
  !>
  !> Releases internal buffers, file handles, and cached weights for all
  !> streams. After this call, handles must not be reused.
  !>
  !> @param[in]  handles      Array of stream handles from tide_init_multi
  !> @param[in]  num_streams  Number of handles in the array
  !> @param[out] ierr         Status code: 0 = success, non-zero = error
  subroutine tide_finalize_all(handles, num_streams, ierr)
    type(c_ptr), intent(in) :: handles(*)
    integer, intent(in) :: num_streams
    integer, intent(out) :: ierr

    ierr = tide_finalize_all_c(handles, int(num_streams, c_int))
  end subroutine tide_finalize_all

  !> @brief Get the number of streams defined in a configuration file.
  !>
  !> Parses the YAML configuration and returns the count of stream entries
  !> without initializing any streams. Useful for allocating the handles
  !> array before calling tide_init_multi.
  !>
  !> @param[in]  config_path  Path to YAML configuration file
  !> @param[out] count        Number of configured streams
  !> @param[out] ierr         Status code: 0 = success, non-zero = error
  subroutine tide_get_stream_count(config_path, count, ierr)
    character(len=*), intent(in) :: config_path
    integer, intent(out) :: count
    integer, intent(out) :: ierr

    integer(c_int) :: c_count

    ierr = tide_get_stream_count_c(trim(config_path) // c_null_char, c_count)
    count = int(c_count)
  end subroutine tide_get_stream_count

  !> @brief Get the health status of a specific stream.
  !>
  !> Returns 0 if the stream is healthy, or a non-zero error code from the
  !> most recent failure.
  !>
  !> @param[in]  handle  Stream handle from tide_init_multi or tide_init
  !> @param[out] status  0 = healthy, non-zero = error code
  subroutine tide_get_stream_status(handle, status)
    type(c_ptr), intent(in) :: handle
    integer, intent(out) :: status

    status = int(tide_get_stream_status_c(handle))
  end subroutine tide_get_stream_status

  !> @brief Get current memory usage across all streams (bytes).
  !>
  !> Computes the total buffer allocation for the given array of stream
  !> handles.
  !>
  !> @param[in]  handles      Array of stream handles
  !> @param[in]  num_streams  Number of handles in the array
  !> @param[out] nbytes       Total bytes allocated for pipeline buffers
  subroutine tide_get_memory_usage(handles, num_streams, nbytes)
    type(c_ptr), intent(in) :: handles(*)
    integer, intent(in) :: num_streams
    integer(c_size_t), intent(out) :: nbytes

    nbytes = tide_get_memory_usage_c(handles, int(num_streams, c_int))
  end subroutine tide_get_memory_usage

  ! ─────────────────────────────────────────────────────────────────────────
  ! Performance Instrumentation convenience subroutines
  ! Validates Requirements: 7.6
  ! ─────────────────────────────────────────────────────────────────────────

  !> @brief Get cumulative per-stage timers for a stream.
  !>
  !> Retrieves the cumulative elapsed time (in seconds) for each pipeline
  !> stage since initialization (or since the last timer reset).
  !> Array indices: (1) I/O, (2) Temporal, (3) Spatial, (4) Vertical, (5) Scaling
  !>
  !> @param[in]  handle  Stream handle
  !> @param[out] timers  Array of 5 doubles with cumulative stage times (seconds)
  !> @param[out] ierr    Status code: 0 = success, non-zero = error
  subroutine tide_get_timers(handle, timers, ierr)
    type(c_ptr), intent(in) :: handle
    real(c_double), intent(out) :: timers(5)
    integer, intent(out) :: ierr

    ierr = tide_get_timers_c(handle, timers)
  end subroutine tide_get_timers

  !> @brief Reset cumulative timers for a stream to zero.
  !>
  !> After this call, tide_get_timers will return zeros until new
  !> advance calls accumulate time.
  !>
  !> @param[in]  handle  Stream handle
  !> @param[out] ierr    Status code: 0 = success, non-zero = error
  subroutine tide_reset_timers(handle, ierr)
    type(c_ptr), intent(in) :: handle
    integer, intent(out) :: ierr

    ierr = tide_reset_timers_c(handle)
  end subroutine tide_reset_timers

  ! ─────────────────────────────────────────────────────────────────────────
  ! Weight Export convenience subroutine
  ! Validates Requirements: 9.5
  ! ─────────────────────────────────────────────────────────────────────────

  !> @brief Export cached interpolation weights to a SCRIP NetCDF file.
  !>
  !> Writes the weight matrix (either loaded from a SCRIP file or computed
  !> by Atlas) for the given stream to a NetCDF file in SCRIP format.
  !>
  !> @param[in]  handle       Stream handle (must have weights computed/loaded)
  !> @param[in]  output_path  Path for the output SCRIP NetCDF file
  !> @param[out] ierr         Status code: 0 = success, non-zero = error
  subroutine tide_export_weights(handle, output_path, ierr)
    type(c_ptr), intent(in) :: handle
    character(len=*), intent(in) :: output_path
    integer, intent(out) :: ierr

    ierr = tide_export_weights_c(handle, trim(output_path) // c_null_char)
  end subroutine tide_export_weights

end module tide_mod
