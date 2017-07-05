////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014-2016, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory.
// Written by the LBANN Research Team (B. Van Essen, et al.) listed in
// the CONTRIBUTORS file. <lbann-dev@llnl.gov>
//
// LLNL-CODE-697807.
// All rights reserved.
//
// This file is part of LBANN: Livermore Big Artificial Neural Network
// Toolkit. For details, see http://software.llnl.gov/LBANN or
// https://github.com/LLNL/LBANN.
//
// Licensed under the Apache License, Version 2.0 (the "Licensee"); you
// may not use this file except in compliance with the License.  You may
// obtain a copy of the License at:
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the license.
//
// lbann_data_reader .hpp .cpp - Input data base class for training, testing
////////////////////////////////////////////////////////////////////////////////

#include "lbann/data_readers/data_reader.hpp"

using namespace std;

namespace lbann {

void generic_data_reader::setup(int base_offset, int batch_stride, int sample_stride, int model_offset,
                       lbann_comm *comm) {
  m_model_offset = model_offset;
  m_base_offset = base_offset;
  m_batch_stride = batch_stride;
  m_sample_stride = sample_stride;
  m_last_mini_batch_stride = batch_stride;
  m_current_mini_batch_idx = 0;
  m_num_iterations_per_epoch = 0;

  /// The amount of space needed will vary based on input layer type,
  /// but the batch size is the maximum space necessary
  Zeros(m_indices_fetched_per_mb, m_batch_size, 1);

  if(comm != NULL) {
    m_use_alt_last_mini_batch_size = true;
    m_num_iterations_per_epoch = m_num_mini_batches_per_reader;
  } else {
    /// By default each data reader will plan to process the entire data set
    m_num_iterations_per_epoch = ceil((float) this->getNumData() / (float) m_batch_size);
  }

  m_current_pos = m_base_offset + m_model_offset;
  if (not m_first_n) {
    std::shuffle(m_shuffled_indices.begin(), m_shuffled_indices.end(),
                 get_data_seq_generator());
  }

}

void generic_data_reader::setup() {
  generic_data_reader::setup(0, m_batch_size);
}

bool generic_data_reader::update() {
  /// Is the mini-batch that is about to finish equal to the second to last mini-batch
  if(m_use_alt_last_mini_batch_size && ((m_current_mini_batch_idx+1) >= (m_num_mini_batches_per_reader-1))) {
    //    std::cout << "Data reader last update update the current position is " << m_current_pos << " and the next postion is going to be " << (m_current_pos + m_last_mini_batch_stride) << " and the number of samples total is " << m_shuffled_indices.size() << std::endl;
    m_current_pos += m_last_mini_batch_stride;
  } else {
    //    std::cout << "Data reader update the current position is " << m_current_pos << " and the next postion is going to be " << (m_current_pos + m_batch_stride) << " and the number of samples total is " << m_shuffled_indices.size() << std::endl;
    m_current_pos += m_batch_stride;
  }

  /// Maintain the current width of the matrix
  Zeros(m_indices_fetched_per_mb, m_indices_fetched_per_mb.Width(), 1);

  if (m_current_pos < (int)m_shuffled_indices.size()) {
    m_current_mini_batch_idx++;
    return true;
  } else {
    if (not m_first_n) {
      std::shuffle(m_shuffled_indices.begin(), m_shuffled_indices.end(),
                   get_data_seq_generator());
    }
    m_current_mini_batch_idx = 0;
    m_current_pos = m_base_offset + m_model_offset;
    return false;
  }
}

int generic_data_reader::getm_batch_size() const {
  if (m_use_alt_last_mini_batch_size &&
      m_current_mini_batch_idx >= (m_num_mini_batches_per_reader-1)) {
    return m_last_mini_batch_size;
  } else {
    return m_batch_size;
  }
}

int generic_data_reader::get_next_position() const {
  /// Is the mini-batch that is about to finish equal to the second to last mini-batch
  if (m_use_alt_last_mini_batch_size &&
      ((m_current_mini_batch_idx+1) >= (m_num_mini_batches_per_reader-1))) {
    return m_current_pos + m_last_mini_batch_stride;
  } else {
    return m_current_pos + m_batch_stride;
  }
}

void generic_data_reader::select_subset_of_data() {
  if(!get_firstN()) {
    std::shuffle(m_shuffled_indices.begin(), m_shuffled_indices.end(), get_data_seq_generator());
  }

  if (not (has_max_sample_count() or has_use_percent() or has_validation_percent())) {
    return;
  }

  if (has_max_sample_count()) {
    size_t count = get_max_sample_count();
    if(count > getNumData()) {
      stringstream err;
      err << __FILE__ << " " << __LINE__
          << " :: generic_data_reader::select_subset_of_data() - max_sample_count=" << count
          << " is > getNumData=" << getNumData();
      throw lbann_exception(err.str());
    }
    m_shuffled_indices.resize(get_max_sample_count());
  } else if (has_use_percent()) {
    m_shuffled_indices.resize(get_use_percent()*getNumData());
  }

  if (has_validation_percent()) {
    long unused = get_validation_percent()*getNumData(); //getNumData() = m_shuffled_indices.size()
    long use_me = getNumData() - unused;
    if (unused > 0) {
      m_unused_indices=std::vector<int>(m_shuffled_indices.begin() + use_me, m_shuffled_indices.end());
      m_shuffled_indices.resize(use_me);
    }
  }

  if(!get_firstN()) {
    std::sort(m_shuffled_indices.begin(), m_shuffled_indices.end());
    std::sort(m_unused_indices.begin(), m_unused_indices.end());
  }
}

void generic_data_reader::use_unused_index_set() {
  m_shuffled_indices.swap(m_unused_indices);
  m_unused_indices.clear();
  std::vector<int>().swap(m_unused_indices); // Trick to force memory reallocation
}

generic_data_reader& generic_data_reader::operator=(const generic_data_reader& source) {
  this->m_batch_size = source.m_batch_size;
  this->m_current_pos = source.m_current_pos;
  this->m_first_n = source.m_first_n;
  this->m_batch_stride = source.m_batch_stride;
  this->m_sample_stride = source.m_sample_stride;
  this->m_base_offset = source.m_base_offset;
  this->m_model_offset = source.m_model_offset;
  this->m_use_alt_last_mini_batch_size = source.m_use_alt_last_mini_batch_size;
  this->m_last_mini_batch_threshold = source.m_last_mini_batch_threshold;
  this->m_last_mini_batch_size = source.m_last_mini_batch_size;
  this->m_last_mini_batch_stride = source.m_last_mini_batch_stride;

  // Vectors implement a deep copy
  this->m_shuffled_indices = source.m_shuffled_indices;
  this->m_unused_indices = source.m_unused_indices;
  return *this;
}

/** \brief Given directory to store checkpoint files, write state to file and add to number of bytes written */
bool generic_data_reader::saveToCheckpointShared(persist& p, const char *name) {
  // rank 0 writes the training state file
  if (p.get_rank() == 0) {
    char fieldname[1024];

    // record minibatch index
    snprintf(fieldname, sizeof(fieldname), "%s_current_mini_batch_idx", name);
    p.write_uint64(persist_type::train, fieldname, (uint64_t) m_current_mini_batch_idx);

    // get size of list of training examples
    int size = m_shuffled_indices.size();

    // record size of ShuffleIndices
    snprintf(fieldname, sizeof(fieldname), "%s_data_size", name);
    p.write_uint64(persist_type::train, fieldname, (uint64_t) size);

    // TODO: each model may have a different position, need to gather and write these
    // record current position within training data
    snprintf(fieldname, sizeof(fieldname), "%s_data_position", name);
    p.write_uint64(persist_type::train, fieldname, (uint64_t) m_current_pos);

    // write list of indices
    snprintf(fieldname, sizeof(fieldname), "%s_data_indices", name);
    p.write_int32_contig(persist_type::train, fieldname, &m_shuffled_indices[0], (uint64_t) size);
  }

  return true;
}

/** \brief Given directory to store checkpoint files, read state from file and add to number of bytes read */
bool lbann::generic_data_reader::loadFromCheckpointShared(persist& p, const char *name) {
  // rank 0 reads the training state file
  if (p.get_rank() == 0) {
    char fieldname[1024];

    // record minibatch index
    uint64_t val;
    snprintf(fieldname, sizeof(fieldname), "%s_current_mini_batch_idx", name);
    p.read_uint64(persist_type::train, fieldname, &val);
    m_current_mini_batch_idx = (int) val;

    // get size of ShuffleIndices
    snprintf(fieldname, sizeof(fieldname), "%s_data_size", name);
    p.read_uint64(persist_type::train, fieldname, &val);
    int size = (int) val;

    // get current position within data
    snprintf(fieldname, sizeof(fieldname), "%s_data_position", name);
    p.read_uint64(persist_type::train, fieldname, &val);
    m_current_pos = (int) val;

    // resize shuffled index array to hold values
    m_shuffled_indices.resize(size);

    // read list of indices
    snprintf(fieldname, sizeof(fieldname), "%s_data_indices", name);
    p.read_int32_contig(persist_type::train, fieldname, &m_shuffled_indices[0], (uint64_t) size);
  }

  // broadcast minibatch index
  MPI_Bcast(&m_current_mini_batch_idx, 1, MPI_INT, 0, MPI_COMM_WORLD);

  // TODO: with multiple readers, make this a scatter
  // broadcast current position
  MPI_Bcast(&m_current_pos, 1, MPI_INT, 0, MPI_COMM_WORLD);

  // broadcast values from rank 0
  int size = m_shuffled_indices.size();
  MPI_Bcast(&size, 1, MPI_INT, 0, MPI_COMM_WORLD);

  // resize shuffled index array to hold values
  if (p.get_rank() != 0) {
    m_shuffled_indices.resize(size);
  }

  // broadcast index array
  MPI_Bcast(&m_shuffled_indices[0], size, MPI_INT, 0, MPI_COMM_WORLD);

  return true;
}

void generic_data_reader::set_file_dir(std::string s) {
  m_file_dir = s;
}

std::string generic_data_reader::get_file_dir() const {
  return m_file_dir;
}

void generic_data_reader::set_data_filename(std::string s) {
  m_data_fn = s;
}

std::string generic_data_reader::get_data_filename() const {
  if (m_data_fn == "") {
    std::stringstream s;
    s << __FILE__ << " " << __LINE__ << " :: you apparently did not call "
      << "set_data_filename; this is an error!";
    throw lbann_exception(s.str());
  }
  return m_data_fn;
}

void generic_data_reader::set_label_filename(std::string s) {
  m_label_fn = s;
}

string generic_data_reader::get_label_filename() const {
  if (m_label_fn == "") {
    std::stringstream s;
    s << __FILE__ << " " << __LINE__ << " :: you apparently did not call "
      << "set_label_filename; this is an error!";
    throw lbann_exception(s.str());
  }
  return m_label_fn;
}

void generic_data_reader::set_max_sample_count(size_t s) {
  m_max_sample_count = s;
  m_max_sample_count_was_set = true;
}

size_t generic_data_reader::get_max_sample_count() const {
  return m_max_sample_count;
}

bool generic_data_reader::has_max_sample_count() const {
  return m_max_sample_count_was_set;
}

void generic_data_reader::set_firstN(bool b) {
  m_first_n = b;
}

bool generic_data_reader::get_firstN() const {
  return m_first_n;
}

void generic_data_reader::set_validation_percent(double s) {
  if (s < 0 or s > 1.0) {
    stringstream err;
    err << __FILE__ << " " << __LINE__ << " :: set_validation_percent() - must be: s >= 0, s <= 1.0; you passed: " << s;
    throw lbann_exception(err.str());
  }
  m_validation_percent = s;
}

bool generic_data_reader::has_validation_percent() const {
  if (m_validation_percent == -1) {
    return false;
  }
  return true;
}

double generic_data_reader::get_validation_percent() const {
  return m_validation_percent;
}

void generic_data_reader::set_use_percent(double s) {
  if (s < 0 or s > 1.0) {
    stringstream err;
    err << __FILE__ << " " << __LINE__ << " :: set_use_percent() - must be: s >= 0, s <= 1.0; you passed: " << s;
    throw lbann_exception(err.str());
  }
  m_use_percent = s;
}

bool generic_data_reader::has_use_percent() const {
  if (m_use_percent == -1) {
    return false;
  }
  return true;
}

double generic_data_reader::get_use_percent() const {
  stringstream err;
  if (not has_use_percent()) {
    err << __FILE__ << " " << __LINE__ << " :: you must call set_use_percent()"
        << " but apparently have not done so";
    throw lbann_exception(err.str());
  }
  return m_use_percent;
}

}  // namespace lbann