#ifndef DASH__IO__STORE_HDF5_H__
#define DASH__IO__STORE_HDF5_H__

#include <dash/internal/Config.h>

#ifdef DASH_ENABLE_HDF5

#include <dash/Exception.h>
#include <dash/Init.h>
#include <dash/Array.h>
#include <dash/Matrix.h>

#include <sys/stat.h>

#include <hdf5.h>
#include <hdf5_hl.h>

#include <iostream>
#include <unistd.h>
#include <string>
#include <vector>
#include <array>

#include <mpi.h>

namespace dash {
namespace io {

/**
 * DASH wrapper to store an dash::Array or dash::Matrix
 * in an HDF5 file using parallel IO.
 * All operations are collective.
 */
class StoreHDF {
public:
  /**
  * Options which can be passed to dash::io::StoreHDF::write
  * to specify how existing structures are treated and what
  * metadata is stored.
  *
  * Collective operation.
  */
  typedef struct hdf5_file_options_t {
    /// Overwrite HDF5 file if already existing
    bool          overwrite_file;
    /**
		 * Modify an already existing HDF5 dataset.
     * If the dataset is not existing, throws a runtime error
     */
    bool          modify_dataset;
    /// Store dash pattern characteristics as metadata in HDF5 file
    bool          store_pattern;
    /// Restore pattern from metadata if HDF5 file contains any.
    bool          restore_pattern;
    /// Metadata attribute key in HDF5 file.
    std::string   pattern_metadata_key;
  } hdf5_file_options;

  /**
   * test at compile time if pattern is compatible
   * \return true if pattern is compatible
   */
private:
  template <
    class pattern_t >
  static constexpr bool _compatible_pattern()
  {
    return dash::pattern_partitioning_traits<pattern_t>::type::rectangular &&
           dash::pattern_layout_traits<pattern_t>::type::linear &&
           !dash::pattern_mapping_traits<pattern_t>::type::shifted &&
           !dash::pattern_mapping_traits<pattern_t>::type::diagonal;
    // TODO: check if mapping is regular by checking pattern property
  }

public:
  /**
   * Store all dash::Array values in an HDF5 file using parallel IO.
   * Collective operation.
   * \param array     Array to store
  	 * \param filename  Filename of HDF5 file including extension
   * \param dataset     HDF5 Dataset in which the data is stored
  	 * \param foptions
   */
  template <
    typename value_t,
    typename index_t,
    class    pattern_t >
  typename std::enable_if <
  _compatible_pattern<pattern_t>() &&
  pattern_t::ndim() == 1,
            void >::type
            static write(
              dash::Array<value_t, index_t, pattern_t> & array,
              std::string filename,
              std::string dataset,
              hdf5_file_options foptions = _get_fdefaults())
  {
    auto pattern    = array.pattern();
    auto pat_dims   = pattern.ndim();
    long tilesize   = pattern.blocksize(0);
    // Map native types to HDF5 types
    auto h5datatype = _convertType(array[0]);

    /* HDF5 definition */
    hid_t   file_id;
    hid_t   h5dset;
    hid_t   internal_type;
    hid_t   plist_id; // property list identifier
    hid_t   filespace;
    hid_t   memspace;
    hid_t   attr_id;
    herr_t  status;

    // get hdf pattern layout
    hdf5_pattern_spec<1> ts = _get_pattern_hdf_spec<1>(pattern);

    // setup mpi access
    plist_id = H5Pcreate(H5P_FILE_ACCESS);
    H5Pset_fapl_mpio(plist_id, MPI_COMM_WORLD, MPI_INFO_NULL);

    dash::Shared<int> f_exists;
    if (dash::myid() == 0) {
      if (access( filename.c_str(), F_OK ) != -1) {
        // check if file exists
        f_exists.set(static_cast<int> (H5Fis_hdf5( filename.c_str())));
      } else {
        f_exists.set(-1);
      }
    }
    array.barrier();

    if (foptions.overwrite_file || (f_exists.get() <= 0)) {
      // HD5 create file
      file_id = H5Fcreate( filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, plist_id );
    } else {
      // Open file in RW mode
      file_id = H5Fopen( filename.c_str(), H5F_ACC_RDWR, plist_id );
    }
    // close property list
    H5Pclose(plist_id);

    // Create dataspace
    filespace     = H5Screate_simple(1, ts.data_dimsf, NULL);
    memspace      = H5Screate_simple(1, ts.data_dimsm, NULL);
    internal_type = H5Tcopy(h5datatype);

		if(foptions.modify_dataset){
			// Open dataset in RW mode
			h5dset = H5Dopen(file_id, dataset.c_str(), H5P_DEFAULT);
		} else {
    	// Create dataset
    	h5dset = H5Dcreate(file_id, dataset.c_str(), internal_type, filespace,
                        H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		}
    // Close global dataspace
    H5Sclose(filespace);

    filespace = H5Dget_space(h5dset);

    // Select Hyperslabs in file
    H5Sselect_hyperslab(
      filespace,
      H5S_SELECT_SET,
      ts.offset,
      ts.stride,
      ts.count,
      ts.block);

    // Create property list for collective writes
    plist_id = H5Pcreate(H5P_DATASET_XFER);
    H5Pset_dxpl_mpio(plist_id, H5FD_MPIO_COLLECTIVE);

    DASH_LOG_DEBUG("write completely filled blocks");
    // Write completely filled blocks by pattern
    H5Dwrite(h5dset, internal_type, memspace, filespace,
             plist_id, array.lbegin());

    // write underfilled blocks
    ts = _get_pattern_hdf_spec_underfilled<1>(pattern);
    memspace      = H5Screate_simple(1, ts.data_dimsm, NULL);

    H5Sselect_hyperslab(
      filespace,
      H5S_SELECT_SET,
      ts.offset,
      ts.stride,
      ts.count,
      ts.block);

    DASH_LOG_DEBUG("write partially filled blocks");
    H5Dwrite(h5dset, internal_type, memspace, filespace,
             plist_id, array.lbegin());


    // Add Attributes
    if (foptions.store_pattern) {
      DASH_LOG_DEBUG("write additional attributes to hdf file");
      auto pat_key = foptions.pattern_metadata_key.c_str();
      hid_t attrspace = H5Screate(H5S_SCALAR);
      long attr = (long) tilesize;

			// Delete old attribute when overwriting dataset
			if(foptions.modify_dataset){
				H5Adelete(h5dset, pat_key);
			}
      hid_t attribute_id = H5Acreate(
                             h5dset, pat_key, H5T_NATIVE_LONG,
                             attrspace, H5P_DEFAULT, H5P_DEFAULT);
      H5Awrite(attribute_id, H5T_NATIVE_LONG, &attr);
      H5Aclose(attribute_id);
      H5Sclose(attrspace);
    }

    // Close all
    H5Dclose(h5dset);
    H5Sclose(filespace);
    H5Sclose(memspace);
    H5Tclose(internal_type);
    H5Fclose(file_id);
  }

  /**
  * Store all dash::Matrix values in an HDF5 file using parallel IO
  	* Collective operation.
  * \param array     Array to store
   * \param filename  Filename of HDF5 file including extension
  * \param dataset     HDF5 Dataset in which the data is stored
   * \param foptions
  */
  template <
    typename value_t,
    dim_t    ndim,
    typename index_t,
    typename pattern_t
    >
  typename std::enable_if <
  _compatible_pattern<pattern_t>(),
                      void >::type
                      static write(
                        dash::Matrix<value_t, ndim, index_t, pattern_t> & array,
                        std::string filename,
                        std::string dataset,
                        hdf5_file_options foptions = _get_fdefaults())
  {
    static_assert(
      array.ndim() == pattern_t::ndim(),
      "Pattern dimension has to match matrix dimension");

    auto pattern    = array.pattern();
    auto pat_dims    = pattern.ndim();
    // Map native types to HDF5 types
    auto h5datatype = _convertType(*array.lbegin());


    /* HDF5 definition */
    hid_t   file_id;
    hid_t   h5dset;
    hid_t   internal_type;
    hid_t   plist_id; // property list identifier
    hid_t   filespace;
    hid_t   memspace;
    hid_t   attr_id;
    herr_t  status;

    hdf5_pattern_spec<ndim> ts;

    // get hdf pattern layout
    ts = _get_pattern_hdf_spec<ndim>(pattern);

    // setup mpi access
    plist_id = H5Pcreate(H5P_FILE_ACCESS);
    H5Pset_fapl_mpio(plist_id, MPI_COMM_WORLD, MPI_INFO_NULL);

    dash::Shared<int> f_exists;
    if (dash::myid() == 0) {
      if (access( filename.c_str(), F_OK ) != -1) {
        // check if file exists
        f_exists.set(static_cast<int> (H5Fis_hdf5( filename.c_str())));
      } else {
        f_exists.set(-1);
      }
    }
    array.barrier();

    if (foptions.overwrite_file || (f_exists.get() <= 0)) {
      // HD5 create file
      file_id = H5Fcreate( filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, plist_id );
    } else {
      // Open file in RW mode
      file_id = H5Fopen( filename.c_str(), H5F_ACC_RDWR, plist_id );
    }

    // close property list
    H5Pclose(plist_id);

    // Create dataspace
    filespace     = H5Screate_simple(ndim, ts.data_dimsf, NULL);
    memspace      = H5Screate_simple(ndim, ts.data_dimsm, NULL);
    internal_type = H5Tcopy(h5datatype);

		if(foptions.modify_dataset){
			// Open dataset in RW mode
			h5dset = H5Dopen(file_id, dataset.c_str(), H5P_DEFAULT);
		} else {
    	// Create dataset
    	h5dset = H5Dcreate(file_id, dataset.c_str(), internal_type, filespace,
                        H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		}

    // Close global dataspace
    H5Sclose(filespace);

    filespace = H5Dget_space(h5dset);

    H5Sselect_hyperslab(
      filespace,
      H5S_SELECT_SET,
      ts.offset,
      ts.stride,
      ts.count,
      ts.block);

    // Create property list for collective writes
    plist_id = H5Pcreate(H5P_DATASET_XFER);
    H5Pset_dxpl_mpio(plist_id, H5FD_MPIO_COLLECTIVE);

    // Write completely filled blocks
    H5Dwrite(h5dset, internal_type, memspace, filespace,
             plist_id, array.lbegin());

    // write underfilled blocks
    // get hdf pattern layout
    ts = _get_pattern_hdf_spec_underfilled<ndim>(pattern);
    memspace      = H5Screate_simple(ndim, ts.data_dimsm, NULL);
    H5Sselect_hyperslab(
      filespace,
      H5S_SELECT_SET,
      ts.offset,
      ts.stride,
      ts.count,
      ts.block);

    H5Dwrite(h5dset, internal_type, memspace, filespace,
             plist_id, array.lbegin());

    // Add Attributes
    if (foptions.store_pattern) {
      DASH_LOG_DEBUG("store pattern in hdf5 file");
      auto pat_key = foptions.pattern_metadata_key.c_str();
      long pattern_spec[ndim * 4];

			// Delete old attribute when overwriting dataset
			if(foptions.modify_dataset){
				H5Adelete(h5dset, pat_key);
			}
      // Structure is
      // sizespec, teamspec, blockspec, blocksize
      for (int i = 0; i < ndim; ++i) {
        pattern_spec[i]              = pattern.sizespec().extent(i);
        pattern_spec[i + ndim]       = pattern.teamspec().extent(i);
        pattern_spec[i + (ndim * 2)] = pattern.blockspec().extent(i);
        pattern_spec[i + (ndim * 3)] = pattern.blocksize(i);
      }

      hsize_t attr_len[] = { static_cast<hsize_t> (ndim * 4) };
      hid_t attrspace = H5Screate_simple(1, attr_len, NULL);
      hid_t attribute_id = H5Acreate(
                             h5dset, pat_key, H5T_NATIVE_LONG,
                             attrspace, H5P_DEFAULT, H5P_DEFAULT);
      H5Awrite(attribute_id, H5T_NATIVE_LONG, &pattern_spec);
      H5Aclose(attribute_id);
      H5Sclose(attrspace);
    }

    // Close all
    H5Dclose(h5dset);
    H5Sclose(filespace);
    H5Sclose(memspace);
    H5Tclose(internal_type);
    H5Fclose(file_id);
  }

  /**
  * Read an HDF5 dataset into a dash::Array using parallel IO
  * if the array is already allocated, the size has to match the HDF5 dataset
   * size and all data will be overwritten.
  * Otherwise the array will be allocated.
  *
  * Colletive operation.
  * \param array     Import data in this dash::Array
   * \param filename  Filename of HDF5 file including extension
  * \param dataset     HDF5 Dataset in which the data is stored
   * \param foptions
  */
  template <
    typename value_t,
    typename index_t,
    class    pattern_t >
  typename std::enable_if <
  _compatible_pattern<pattern_t>() &&
  pattern_t::ndim() == 1,
            void >::type
            static read(
              dash::Array<value_t, index_t, pattern_t> & array,
              std::string filename,
              std::string dataset,
              hdf5_file_options foptions = _get_fdefaults())
  {
    long     tilesize;
    int			 rank;
    // HDF5 definition
    hid_t    file_id;
    hid_t    h5dset;
    hid_t    internal_type;
    hid_t    plist_id; // property list identifier
    hid_t    filespace;
    hid_t    memspace;
    // global data dims
    hsize_t  data_dimsf[1];
    herr_t   status;
    // Map native types to HDF5 types
    hid_t    h5datatype;

    // setup mpi access
    plist_id = H5Pcreate(H5P_FILE_ACCESS);
    H5Pset_fapl_mpio(plist_id, MPI_COMM_WORLD, MPI_INFO_NULL);

    // HD5 create file
    file_id = H5Fopen(filename.c_str(), H5P_DEFAULT, plist_id );

    // close property list
    H5Pclose(plist_id);

    // Create dataset
    h5dset = H5Dopen(file_id, dataset.c_str(), H5P_DEFAULT);

    // Get dimensions of data
    filespace     = H5Dget_space(h5dset);
    rank          = H5Sget_simple_extent_ndims(filespace);

    DASH_ASSERT_EQ(rank, 1, "Data dimension of HDF5 dataset is not 1");

    status        = H5Sget_simple_extent_dims(filespace, data_dimsf, NULL);

    // Initialize DASH Array
    // no explicit pattern specified / try to load pattern from hdf5 file
    auto pat_key = foptions.pattern_metadata_key.c_str();
    // Check if matrix is already allocated
    bool is_alloc  = (array.size() != 0);

    if (!is_alloc												  // not allocated
        && foptions.restore_pattern			  // pattern should be restored
        && H5Aexists(h5dset, pat_key)) { // hdf5 contains pattern
      hid_t attrspace      = H5Screate(H5S_SCALAR);
      hid_t attribute_id  = H5Aopen(h5dset, pat_key, H5P_DEFAULT);
      H5Aread(attribute_id, H5T_NATIVE_LONG, &tilesize);
      H5Aclose(attribute_id);
      H5Sclose(attrspace);
      // TODO: Array allocate(pattern) is currently not working.
#if 0
      const pattern_t pattern(
        dash::SizeSpec<1>(static_cast<size_t>(data_dimsf[0])),
        dash::DistributionSpec<1>(dash::TILE(tilesize)),
        dash::TeamSpec<1>(),
        dash::Team::All());

      array.allocate(pattern);
#else
      array.allocate(data_dimsf[0], dash::TILE(tilesize));
#endif
    } else if (is_alloc) {
      DASH_LOG_DEBUG("Array already allocated");
      // Check if array size matches data extents
      DASH_ASSERT_EQ(
        data_dimsf[0],
        array.size(),
        "Array size does not match data extents");
    } else {
      // Auto deduce pattern
#if 0
      const pattern_t pattern(
        dash::SizeSpec<1>(static_cast<size_t>(data_dimsf[0])),
        dash::DistributionSpec<1>(),
        dash::TeamSpec<1>(),
        dash::Team::All());
      array.allocate(pattern);
#else
      array.allocate(data_dimsf[0], dash::CYCLIC);
#endif
    }
    auto pattern    = array.pattern();
    h5datatype = _convertType(array[0]); // hack

    // get hdf pattern layout
    hdf5_pattern_spec<1> ts = _get_pattern_hdf_spec<1>(pattern);

    // Create HDF5 memspace
    memspace      = H5Screate_simple(1, ts.data_dimsm, NULL);
    internal_type  = H5Tcopy(h5datatype);

    // Select Hyperslabs in file
    H5Sselect_hyperslab(
      filespace,
      H5S_SELECT_SET,
      ts.offset,
      ts.stride,
      ts.count,
      ts.block);

    // Create property list for collective reads
    plist_id = H5Pcreate(H5P_DATASET_XFER);
    H5Pset_dxpl_mpio(plist_id, H5FD_MPIO_COLLECTIVE);

    // read data
    DASH_LOG_DEBUG("read completely filled blocks");
    H5Dread(h5dset, internal_type, memspace, filespace,
            plist_id, array.lbegin());

    // read underfilled blocks
    // get hdf pattern layout
    ts = _get_pattern_hdf_spec_underfilled<1>(pattern);
    memspace      = H5Screate_simple(1, ts.data_dimsm, NULL);

    H5Sselect_hyperslab(
      filespace,
      H5S_SELECT_SET,
      ts.offset,
      ts.stride,
      ts.count,
      ts.block);

    // Read underfilled blocks by pattern
    DASH_LOG_DEBUG("read partially filled blocks");
    H5Dread(h5dset, internal_type, memspace, filespace,
            plist_id, array.lbegin());

    // Close all
    H5Dclose(h5dset);
    H5Sclose(filespace);
    H5Sclose(memspace);
    H5Tclose(internal_type);
    H5Fclose(file_id);
  }

  /**
   * Read an HDF5 dataset into a dash::Matrix using parallel IO
   * if the matrix is already allocated, the sizes have to match
  	 * the HDF5 dataset sizes and all data will be overwritten.
   * Otherwise the matrix will be allocated.
   * Collective operation
   * \param matrix    Import data in this dash::Matrix
  	 * \param filename  Filename of HDF5 file including extension
   * \param dataset     HDF5 Dataset in which the data is stored
  	 * \param foptions
   */
  template <
    typename value_t,
    dim_t    ndim,
    typename index_t,
    class    pattern_t >
  typename std::enable_if <
  _compatible_pattern<pattern_t>(),
                      void >::type
                      static read(
                        dash::Matrix <
                        value_t,
                        ndim,
                        index_t,
                        pattern_t > &matrix,
                        std::string filename,
                        std::string dataset,
                        hdf5_file_options foptions = _get_fdefaults())
  {

    // HDF5 definition
    hid_t   file_id;
    hid_t   h5dset;
    hid_t   internal_type;
    hid_t   plist_id; // property list identifier
    hid_t   filespace;
    hid_t   memspace;
    // global data dims
    hsize_t data_dimsf[ndim];
    herr_t  status;
    // Map native types to HDF5 types
    hid_t h5datatype;
    // rank of hdf5 dataset
    int      rank;
    hdf5_pattern_spec<ndim> ts;

    // setup mpi access
    plist_id = H5Pcreate(H5P_FILE_ACCESS);
    H5Pset_fapl_mpio(plist_id, MPI_COMM_WORLD, MPI_INFO_NULL);

    // HD5 create file
    file_id = H5Fopen(filename.c_str(), H5P_DEFAULT, plist_id );

    // close property list
    H5Pclose(plist_id);

    // Create dataset
    h5dset = H5Dopen(file_id, dataset.c_str(), H5P_DEFAULT);

    // Get dimensions of data
    filespace     = H5Dget_space(h5dset);
    rank          = H5Sget_simple_extent_ndims(filespace);

    DASH_ASSERT_EQ(rank, ndim,
                   "Data dimension of HDF5 dataset does not match matrix dimension");

    status        = H5Sget_simple_extent_dims(filespace, data_dimsf, NULL);

    std::array<size_t, ndim>             size_extents;
    std::array<size_t, ndim>              team_extents;
    std::array<dash::Distribution, ndim> dist_extents;
    long hdf_dash_pattern[ndim * 4];

    // set matrix size according to hdf5 dataset dimensions
    for (int i = 0; i < ndim; ++i) {
      size_extents[i] = data_dimsf[i];
    }

    // Check if file contains DASH metadata and recreate the pattern
    auto pat_key   = foptions.pattern_metadata_key.c_str();
    // Check if matrix is already allocated
    bool is_alloc  = (matrix.size() != 0);

    if (!is_alloc												// not allocated
        && foptions.restore_pattern			// pattern should be restored
        && H5Aexists(h5dset, pat_key)) { // hdf5 contains pattern
      hsize_t attr_len[]  = { ndim * 4};
      hid_t attrspace      = H5Screate_simple(1, attr_len, NULL);
      hid_t attribute_id  = H5Aopen(h5dset, pat_key, H5P_DEFAULT);

      H5Aread(attribute_id, H5T_NATIVE_LONG, hdf_dash_pattern);
      H5Aclose(attribute_id);
      H5Sclose(attrspace);

      for (int i = 0; i < ndim; ++i) {
        size_extents[i]  = static_cast<size_t> (hdf_dash_pattern[i]);
        team_extents[i]  = static_cast<size_t> (hdf_dash_pattern[i + ndim]);
        dist_extents[i]  = dash::TILE(hdf_dash_pattern[i + (ndim * 3)]);
      }
      DASH_LOG_DEBUG("Created pattern according to metadata");

      const pattern_t pattern(
        dash::SizeSpec<ndim>(size_extents),
        dash::DistributionSpec<ndim>(dist_extents),
        dash::TeamSpec<ndim>(team_extents),
        dash::Team::All());

      // Allocate DASH Matrix
      matrix.allocate(pattern);

    } else if (is_alloc) {
      DASH_LOG_DEBUG("Matrix already allocated");
      // Check if matrix extents match data extents
      auto pattern_extents = matrix.pattern().extents();
      for (int i = 0; i < ndim; ++i) {
        DASH_ASSERT_EQ(
          size_extents[i],
          pattern_extents[i],
          "Matrix extents do not match data extents");
      }
    } else {
      // Auto deduce pattern
      const pattern_t pattern(
        dash::SizeSpec<ndim>(size_extents),
        dash::DistributionSpec<ndim>(),
        dash::TeamSpec<ndim>(),
        dash::Team::All());

      matrix.allocate(pattern);
    }

    h5datatype = _convertType(*matrix.lbegin()); // hack
    internal_type = H5Tcopy(h5datatype);

    // setup extends per dimension
    auto pattern = matrix.pattern();
    //DASH_LOG_DEBUG("Pattern", pattern);
    ts = _get_pattern_hdf_spec<ndim>(pattern);

    // Create dataspace
    memspace      = H5Screate_simple(ndim, ts.data_dimsm, NULL);

    H5Sselect_hyperslab(
      filespace,
      H5S_SELECT_SET,
      ts.offset,
      ts.stride,
      ts.count,
      ts.block);

    // Create property list for collective reads
    plist_id = H5Pcreate(H5P_DATASET_XFER);
    H5Pset_dxpl_mpio(plist_id, H5FD_MPIO_COLLECTIVE);

    // read data
    H5Dread(h5dset, internal_type, memspace, filespace,
            plist_id, matrix.lbegin());

    // read underfilled blocks
    // get hdf pattern layout
    ts = _get_pattern_hdf_spec_underfilled<ndim>(pattern);
    memspace      = H5Screate_simple(ndim, ts.data_dimsm, NULL);

    H5Sselect_hyperslab(
      filespace,
      H5S_SELECT_SET,
      ts.offset,
      ts.stride,
      ts.count,
      ts.block);

    // Read underfilled blocks by pattern
    H5Dread(h5dset, internal_type, memspace, filespace,
            plist_id, matrix.lbegin());

    // Close all
    H5Dclose(h5dset);
    H5Sclose(filespace);
    H5Sclose(memspace);
    H5Tclose(internal_type);
    H5Fclose(file_id);
  }

public:
  /**
  * Returns default file options
   * \return hdf5_file_options struct
  */
  static inline hdf5_file_options get_default_options()
  {
    return _get_fdefaults();
  }

  /**
  * hdf5 pattern specification for parallel IO
  */
  template <
    dim_t ndim >
  struct hdf5_pattern_spec {
    hsize_t data_dimsf[ndim];
    hsize_t data_dimsm[ndim];
    hsize_t count[ndim];
    hsize_t stride[ndim];
    hsize_t offset[ndim];
    hsize_t block[ndim];
  };

private:
  static inline hdf5_file_options _get_fdefaults()
  {
    hdf5_file_options fopt;
    fopt.overwrite_file = true;
    fopt.modify_dataset = false;
    fopt.store_pattern = true;
    fopt.restore_pattern = true;
    fopt.pattern_metadata_key = "DASH_PATTERN";

    return fopt;
  }

  /**
   * convert a dash pattern into a hdf5 pattern
   * \param pattern_t pattern
   * \return hdf5_pattern_spec<ndim>
   */
  template <
    dim_t ndim,
    class pattern_t >
  typename std::enable_if <
  _compatible_pattern<pattern_t>(),
                      hdf5_pattern_spec<ndim >>::type
                      static inline _get_pattern_hdf_spec(
                        pattern_t pattern)
  {
    hdf5_pattern_spec<ndim> ts;
    // setup extends per dimension
    for (int i = 0; i < ndim; ++i) {
      auto tilesize    = pattern.blocksize(i);
      ts.data_dimsf[i] = pattern.extent(i);
      ts.data_dimsm[i] = (pattern.local_extent(i) / tilesize) * tilesize;
      // number of tiles in this dimension
      ts.count[i]      = pattern.local_extent(i) / tilesize;
      ts.offset[i]     = pattern.local_block(0).offset(i);
      ts.block[i]      = pattern.blocksize(i);
      ts.stride[i]     = pattern.teamspec().extent(i) * ts.block[i];
    }
    return ts;
  }

  /**
   * get the layout of the last underfilled block of a dash::BlockPattern
   * if the calling unit does not have any underfilled blocks, a zero size
   * block is returned.
   * \param pattern_t pattern
   * \return hdf5_pattern_spec<ndim>
   */
  template <
    dim_t ndim,
    class pattern_t >
  typename std::enable_if <
  _compatible_pattern<pattern_t>(),
                      hdf5_pattern_spec<ndim> >::type
                      static inline _get_pattern_hdf_spec_underfilled(
                        pattern_t pattern)
  {
    hdf5_pattern_spec<ndim> ts;

    for (int i = 0; i < ndim ; i++) {
      long tilesize    = pattern.blocksize(i);
      long localsize   = pattern.local_extent(i);
      long localblocks = localsize / tilesize;
      long lfullsize   = localblocks * tilesize;

      ts.data_dimsf[i] = pattern.extent(i);
      ts.data_dimsm[i] = localsize - lfullsize;
      ts.stride[i]     = tilesize;
      if (localsize != lfullsize) {
        ts.count[i]    = 1;
        ts.offset[i]   = pattern.local_block(localblocks).offset(i);
        ts.block[i]    = localsize - lfullsize;
      } else {
        ts.count[i]    = 0;
        ts.offset[i]   = 0;
        ts.block[i]    = 0;
      }
    }
    return ts;
  }

private:
  static inline hid_t _convertType(int t)
  {
    return H5T_NATIVE_INT;
  }
  static inline hid_t _convertType(long t)
  {
    return H5T_NATIVE_LONG;
  }
  static inline hid_t _convertType(float t)
  {
    return H5T_NATIVE_FLOAT;
  }
  static inline hid_t _convertType(double t)
  {
    return H5T_NATIVE_DOUBLE;
  }

};

} // namespace io
} // namespace dash

#include <dash/io/HDF5Stream.h>

#endif // DASH_ENABLE_HDF5

#endif