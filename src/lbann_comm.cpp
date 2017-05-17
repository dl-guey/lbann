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
// lbann_comm .hpp .cpp - LBANN communication utilities
////////////////////////////////////////////////////////////////////////////////

#include "lbann/lbann_comm.hpp"
#include "lbann/utils/lbann_exception.hpp"
#include "mpi.h"
#include <sstream>

using namespace std;
using namespace El;

// Error utility macro
#ifdef LBANN_DEBUG
#define checkMPI(mpi_call) {                                            \
    const int status = mpi_call;                                        \
    if(status != MPI_SUCCESS) {                                         \
      char error_string[MPI_MAX_ERROR_STRING];                          \
      int error_string_len;                                             \
      MPI_Error_string(status, error_string, &error_string_len);        \
      std::cerr << "MPI error: " << std::string(error_string, error_string_len) << "\n"; \
      std::cerr << "Error at " << __FILE__ << ":" << __LINE__ << "\n";  \
      throw lbann::lbann_exception("MPI error");                        \
    }                                                                   \
  }
#else
#define checkMPI(status) status
#endif // #ifdef LBANN_DEBUG

lbann::lbann_comm::lbann_comm(int _procs_per_model) :
  procs_per_model(_procs_per_model), num_model_barriers(0),
  num_intermodel_barriers(0), num_global_barriers(0), bytes_sent(0),
  bytes_received(0) {

  // Initialize parameters
  int world_size = mpi::Size(mpi::COMM_WORLD);
  if (procs_per_model == 0) {
    procs_per_model = world_size;
  }
  num_models = world_size / procs_per_model;
  model_rank = mpi::Rank(mpi::COMM_WORLD) / procs_per_model;
  rank_in_model = mpi::Rank(mpi::COMM_WORLD) % procs_per_model;

  // Check if parameters are valid
  if (procs_per_model > world_size) {
    stringstream err;
    err << __FILE__ << " " << __LINE__
        << " :: Not enough processes to create one model; procs_per_model: "
        << procs_per_model << " is larger than world_size: " << world_size;
    throw lbann_exception(err.str());
  }
  if (world_size % procs_per_model != 0) {
    stringstream err;
    err << __FILE__ << " " << __LINE__ 
        << " :: Procs per model does not divide total number of procs; procs_per_model: " 
        << procs_per_model << " total number of procs (world size): " << world_size;
    throw lbann_exception(err.str());
  }

  // Initialize model and intermodel communicators
  mpi::Split(mpi::COMM_WORLD, model_rank, rank_in_model, model_comm);
  mpi::Split(mpi::COMM_WORLD, rank_in_model, model_rank, intermodel_comm);

  // Initialize Elemental grid
  grid = new Grid(model_comm);

  // Initialize node communicators
  setup_node_comm();
  procs_per_node = mpi::Size(node_comm);
  rank_in_node = mpi::Rank(node_comm);
  
}

lbann::lbann_comm::~lbann_comm() {
  delete grid;
  mpi::Free(model_comm);
  mpi::Free(intermodel_comm);
  mpi::Free(node_comm);
  for (auto&& buf_vec : collective_bufs) {
    for (auto&& buf : buf_vec.second) {
      delete[] buf;
    }
  }
}

void lbann::lbann_comm::intermodel_sum_matrix(Mat& mat) {
  bytes_sent += sizeof(DataType) * mat.Height() * mat.Width();
  AllReduce(mat, intermodel_comm, mpi::SUM);
  bytes_received += sizeof(DataType) * mat.Height() * mat.Width();
}

void lbann::lbann_comm::intermodel_sum_matrix(DistMat& mat) {
  bytes_sent += sizeof(DataType) * mat.LocalHeight() * mat.LocalWidth();
  AllReduce(mat, intermodel_comm, mpi::SUM);
  bytes_received += sizeof(DataType) * mat.LocalHeight() * mat.LocalWidth();
}

/*void lbann::lbann_comm::nb_intermodel_sum_matrix(Mat& mat, mpi::Request& req) {
  MPI_Iallreduce(MPI_IN_PLACE, mat.Buffer(),
                 mat.Height() * mat.Width(), DataTypeMPI, MPI_SUM,
                 intermodel_comm.comm, &req);
}

void lbann::lbann_comm::nb_intermodel_sum_matrix(DistMat& mat,
                                                 mpi::Request& req) {
  // Note: This reaches into the Elemental internals where presently
  // mpi::Request is a typedef of MPI_Request and the MPI communicator
  // is mpi::Comm::comm.
  MPI_Iallreduce(MPI_IN_PLACE, mat.Buffer(),
                 mat.LocalHeight() * mat.LocalWidth(), DataTypeMPI, MPI_SUM,
                 intermodel_comm.comm, &req);
                 }*/

void lbann::lbann_comm::intermodel_broadcast_matrix(Mat& mat, int root) {
  Broadcast(mat, intermodel_comm, root);
}

void lbann::lbann_comm::intermodel_broadcast_matrix(DistMat& mat, int root) {
  Broadcast(mat, intermodel_comm, root);
}

/*void lbann::lbann_comm::nb_intermodel_broadcast_matrix(Mat& mat, int root,
                                                       mpi::Request& req) {
  MPI_Ibcast(mat.Buffer(), mat.Height() * mat.Width(), DataTypeMPI, root,
             intermodel_comm.comm, &req);
}

void lbann::lbann_comm::nb_intermodel_broadcast_matrix(DistMat& mat, int root,
                                                       mpi::Request& req) {
  MPI_Ibcast(mat.Buffer(), mat.LocalHeight() * mat.LocalWidth(), DataTypeMPI,
             root, intermodel_comm.comm, &req);
             }*/

void lbann::lbann_comm::intermodel_barrier() {
  ++num_intermodel_barriers;
  mpi::Barrier(intermodel_comm);
}

void lbann::lbann_comm::model_barrier() {
  ++num_model_barriers;
  mpi::Barrier(model_comm);
}

void lbann::lbann_comm::global_barrier() {
  ++num_global_barriers;
  mpi::Barrier(mpi::COMM_WORLD);
}

void lbann::lbann_comm::send(Mat& mat, int model, int rank) {
  send(mat.Buffer(), mat.Height() * mat.Width(), model, rank);
}

void lbann::lbann_comm::send(DistMat& mat, int model, int rank) {
  send(mat.Buffer(), mat.LocalHeight() * mat.LocalWidth(), model, rank);
}

void lbann::lbann_comm::nb_send(Mat& mat, int model, int rank,
                                mpi::Request<DataType>& req) {
  nb_send(mat.Buffer(), mat.Height() * mat.Width(), model, rank, req);
}

void lbann::lbann_comm::nb_send(DistMat& mat, int model, int rank,
                                mpi::Request<DataType>& req) {
  nb_send(mat.Buffer(), mat.LocalHeight() * mat.LocalWidth(), model, rank, req);
}

void lbann::lbann_comm::recv(Mat& mat, int model, int rank) {
  recv(mat.Buffer(), mat.Height() * mat.Width(), model, rank);
}

void lbann::lbann_comm::recv(DistMat& mat, int model, int rank) {
  recv(mat.Buffer(), mat.LocalHeight() * mat.LocalWidth(), model, rank);
}

void lbann::lbann_comm::recv(Mat& mat) {
  recv(mat.Buffer(), mat.Height() * mat.Width());
}

void lbann::lbann_comm::recv(DistMat& mat) {
  recv(mat.Buffer(), mat.LocalHeight() * mat.LocalWidth());
}

void lbann::lbann_comm::nb_recv(Mat& mat, int model, int rank,
                                mpi::Request<DataType>& req) {
  nb_recv(mat.Buffer(), mat.Height() * mat.Width(), model, rank, req);
}

void lbann::lbann_comm::nb_recv(DistMat& mat, int model, int rank,
                                mpi::Request<DataType>& req) {
  nb_recv(mat.Buffer(), mat.LocalHeight() * mat.LocalWidth(), model, rank, req);
}

void lbann::lbann_comm::nb_recv(Mat& mat, mpi::Request<DataType>& req) {
  nb_recv(mat.Buffer(), mat.Height() * mat.Width(), req);
}

void lbann::lbann_comm::nb_recv(DistMat& mat, mpi::Request<DataType>& req) {
  nb_recv(mat.Buffer(), mat.LocalHeight() * mat.LocalWidth(), req);
}

void lbann::lbann_comm::broadcast(Mat& mat,
                                  std::vector<int>& dests, int root) {
  broadcast(mat.Buffer(), mat.Height() * mat.Width(), dests, root);
}

void lbann::lbann_comm::broadcast(DistMat& mat,
                                  std::vector<int>& dests, int root) {
  broadcast(mat.Buffer(), mat.LocalHeight() * mat.LocalWidth(), dests, root);
}

void lbann::lbann_comm::intermodel_allreduce(
  Mat& mat, int max_recv_count,
  std::function<uint8_t*(Mat&, IR, IR, int&, bool)> send_transform,
  std::function<int(uint8_t*, Mat&)> recv_transform,
  std::function<int(uint8_t*, Mat&)> recv_apply_transform) {
  // If not a power-of-2, we can't use the recursive doubling.
  const int nprocs = get_num_models();
  if (nprocs & (nprocs - 1)) {
    pe_ring_allreduce(intermodel_comm, mat, max_recv_count,
                      send_transform, recv_transform,
                      recv_apply_transform);
  } else {
    // TODO: Don't hardcode this.
    if (mat.Height() <= 64 && mat.Width() <= 64) {
      recursive_doubling_allreduce_pow2(
        intermodel_comm, mat, max_recv_count,
        send_transform, recv_apply_transform);
    } else {
      pe_ring_allreduce(intermodel_comm, mat, max_recv_count,
                        send_transform, recv_transform,
                        recv_apply_transform);
    }
  }
}

void lbann::lbann_comm::recursive_doubling_allreduce_pow2(
  mpi::Comm comm, Mat& mat, int max_recv_count,
  std::function<uint8_t*(Mat&, IR, IR, int&, bool)> send_transform,
  std::function<int(uint8_t*, Mat&)> recv_apply_transform) {
  const int rank = mpi::Rank(comm);
  const int nprocs = mpi::Size(comm);
  // This implementation requires a power-of-2 number of processes.
  if (nprocs & (nprocs - 1)) {
    return;
  }
  uint8_t* recv_buf = get_collective_buffer(max_recv_count);
  unsigned int mask = 1;
  while (mask < nprocs) {
    int partner = rank ^ mask;  // The rank we exchange with this step.
    // Transform the data we want to send.
    int send_size;
    uint8_t* send_buf = send_transform(mat, ALL, ALL, send_size, false);
    bytes_sent += send_size;
    mpi::SendRecv(send_buf, send_size, partner,
                  recv_buf, max_recv_count, partner, comm);
    // Transform and reduce the received data.
    int recv_size = recv_apply_transform(recv_buf, mat);
    bytes_received += recv_size;
    mask <<= 1;
  }
}

void lbann::lbann_comm::pe_ring_allreduce(
  mpi::Comm comm, Mat& mat, int max_recv_count,
  std::function<uint8_t*(Mat&, IR, IR, int&, bool)> send_transform,
  std::function<int(uint8_t*, Mat&)> recv_transform,
  std::function<int(uint8_t*, Mat&)> recv_apply_transform) {
  const int rank = mpi::Rank(comm);
  const int nprocs = mpi::Size(comm);
  // Compute the number of columns each processor sends.
  // If it doesn't divide evenly, give one extra to the earlier ranks.
  const Int cols_per_proc = mat.Width() / nprocs;
  const Int cols_remainder = mat.Width() % nprocs;
  // Compute the lengths/ends of the slices.
  std::vector<Int> slice_lengths(nprocs, cols_per_proc);
  for (int i = 0; i < cols_remainder; ++i) {
    slice_lengths[i] += 1;
  }
  std::vector<Int> slice_ends(nprocs);
  std::partial_sum(slice_lengths.begin(), slice_lengths.end(),
                   slice_ends.begin());
  uint8_t* recv_buf = get_collective_buffer(max_recv_count);
  // Local slice of our accumulated data.
  auto accum_view = mat(ALL, IR(slice_ends[rank] - slice_lengths[rank],
                                slice_ends[rank]));
  // Do a pairwise-exchange reduce-scatter.
  for (int step = 1; step < nprocs; ++step) {
    // Compute where we send to/receive from.
    const int dst = (rank + step) % nprocs;
    const int src = (rank - step + nprocs) % nprocs;
    // Transform the data we send. We do not look at the same chunk of data
    // twice.
    int send_size;
    uint8_t* send_buf = send_transform(
      mat, ALL, IR(slice_ends[dst] - slice_lengths[dst], slice_ends[dst]),
      send_size, true);
    bytes_sent += send_size;
    mpi::SendRecv(send_buf, send_size, dst,
                  recv_buf, max_recv_count, src, comm);
    int recv_size = recv_apply_transform(recv_buf, accum_view);
    bytes_received += recv_size;
  }
  // Do a ring allgather.
  const int src = (rank - 1 + nprocs) % nprocs;
  const int dst = (rank + 1) % nprocs;
  // Apply the transform to our locally-accumulated slice of the data.
  int send_size;
  // Do the first step where we forward our local data.
  {
    uint8_t* send_buf = send_transform(
      mat, ALL, IR(slice_ends[rank] - slice_lengths[rank], slice_ends[rank]),
      send_size, false);
    const int data_src = (rank - 1 + nprocs) % nprocs;
    bytes_sent += send_size;
    mpi::SendRecv(send_buf, send_size, dst,
                  recv_buf, max_recv_count, src, comm);
    auto recv_view = mat(ALL,
                         IR(slice_ends[data_src] - slice_lengths[data_src],
                            slice_ends[data_src]));
    int recv_size = recv_transform(recv_buf, recv_view);
    bytes_received += recv_size;
    send_size = recv_size;
  }
  // Now do the remaining nprocs - 2 steps.
  // We always send from recv_buf and receive to recv_buf2, swapping
  // pointers to avoid copying.
  uint8_t* recv_buf2 = get_collective_buffer(max_recv_count, 1);
  for (int step = 1; step < nprocs - 1; ++step) {
    // Compute where the data we get is coming from.
    const int data_src = (rank - step - 1 + nprocs) % nprocs;
    auto recv_view = mat(ALL,
                         IR(slice_ends[data_src] - slice_lengths[data_src],
                            slice_ends[data_src]));
    bytes_sent += send_size;
    mpi::SendRecv(recv_buf, send_size, dst,
                  recv_buf2, max_recv_count, src, comm);
    int recv_size = recv_transform(recv_buf2, recv_view);
    bytes_received += recv_size;
    // Swap the send and receive buffers.
    std::swap(recv_buf, recv_buf2);
    send_size = recv_size;
  }
}

void lbann::lbann_comm::ring_allreduce(
  mpi::Comm comm, Mat& mat, int max_recv_count,
  std::function<uint8_t*(Mat&, IR, IR, int&, bool)> send_transform,
  std::function<int(uint8_t*, Mat&)> recv_transform,
  std::function<int(uint8_t*, Mat&)> recv_apply_transform) {
  const int rank = mpi::Rank(comm);
  const int nprocs = mpi::Size(comm);
  // Compute the number of columns each processor sends.
  const Int cols_per_proc = mat.Width() / nprocs;
  const Int cols_remainder = mat.Width() % nprocs;
  // Compute the lengths/ends of the slices.
  std::vector<Int> slice_lengths(nprocs, cols_per_proc);
  for (int i = 0; i < cols_remainder; ++i) {
    slice_lengths[i] += 1;
  }
  std::vector<Int> slice_ends(nprocs);
  std::partial_sum(slice_lengths.begin(), slice_lengths.end(),
                   slice_ends.begin());
  uint8_t* recv_buf = get_collective_buffer(max_recv_count);
  // Compute source/destination in the ring.
  const int src = (rank - 1 + nprocs) % nprocs;
  const int dst = (rank + 1) % nprocs;
  // Do a ring-based reduce-scatter.
  // This is like the pairwise-exchange reduce-scatter except instead of
  // rank i accumulating only slice i, the slices are cycled around and
  // each node accumulates its portion into the slice when it passes
  // through. After the nprocs-1 steps slice k will be on rank
  // (k + nprocs - 1) % nprocs.
  for (int step = 0; step < nprocs - 1; ++step) {
    // Compute the slices to send/recv.
    const int send_slice = (rank - step + nprocs) % nprocs;
    const int recv_slice = (rank - step - 1 + nprocs) % nprocs;
    // Transform the data to send.
    int send_size;
    uint8_t* send_buf = send_transform(
      mat, ALL, IR(slice_ends[send_slice] - slice_lengths[send_slice],
                   slice_ends[send_slice]), send_size, false);
    mpi::SendRecv(send_buf, send_size, dst,
                  recv_buf, max_recv_count, src, comm);
    auto recv_view = mat(
      ALL, IR(slice_ends[recv_slice] - slice_lengths[recv_slice],
              slice_ends[recv_slice]));
    int recv_size = recv_apply_transform(recv_buf, recv_view);
  }
  // Do a ring allgather, first applying the transform to local data.
  int send_size;
  {
    const int send_slice = (rank + 1) % nprocs;
    const int recv_slice = rank;
    uint8_t* send_buf = send_transform(
      mat, ALL, IR(slice_ends[send_slice] - slice_lengths[send_slice],
                   slice_ends[send_slice]), send_size, false);
    mpi::SendRecv(send_buf, send_size, dst,
                  recv_buf, max_recv_count, src, comm);
    auto recv_view = mat(ALL,
                         IR(slice_ends[recv_slice] - slice_lengths[recv_slice],
                            slice_ends[recv_slice]));
    int recv_size = recv_transform(recv_buf, recv_view);
    send_size = recv_size;
  }
  uint8_t* recv_buf2 = get_collective_buffer(max_recv_count, 1);
  for (int step = 1; step < nprocs - 1; ++step) {
    const int send_slice = (rank - step + 1 + nprocs) % nprocs;
    const int recv_slice = (rank - step + nprocs) % nprocs;
    auto recv_view = mat(ALL,
                         IR(slice_ends[recv_slice] - slice_lengths[recv_slice],
                            slice_ends[recv_slice]));
    mpi::SendRecv(recv_buf, send_size, dst,
                  recv_buf2, max_recv_count, src, comm);
    int recv_size = recv_transform(recv_buf2, recv_view);
    // Swap the send and receive buffers.
    std::swap(recv_buf, recv_buf2);
    send_size = recv_size;
  }
}

void lbann::lbann_comm::setup_node_comm() {
 
  // Get string specifying compute node
  char node_name[MPI_MAX_PROCESSOR_NAME];
  int node_name_len;
  checkMPI(MPI_Get_processor_name(node_name, &node_name_len));
  const std::string node_string(node_name);

  // Hash node names and split MPI processes
  int hash = std::hash<std::string>()(node_string);
  hash = hash >= 0 ? hash : -hash;  // Make sure hash is non-negative
  mpi::Comm hash_comm;
  mpi::Split(mpi::COMM_WORLD, hash, mpi::Rank(mpi::COMM_WORLD), hash_comm);
  const int hash_comm_size = mpi::Size(hash_comm);

  // Compare node names and split MPI processes
  char* node_name_list = new char[hash_comm_size*MPI_MAX_PROCESSOR_NAME];
  checkMPI(MPI_Allgather(node_name, MPI_MAX_PROCESSOR_NAME, MPI_CHAR,
                         node_name_list, MPI_MAX_PROCESSOR_NAME, MPI_CHAR,
                         hash_comm.comm));
  int node_num = mpi::Rank(hash_comm);
  for(int i=0; i<hash_comm_size; ++i) {
    const std::string other_node_string(node_name_list + i*MPI_MAX_PROCESSOR_NAME);
    if(node_string == other_node_string) {
      node_num = i;
      break;
    }
  }
  delete[] node_name_list;
  mpi::Split(hash_comm, node_num, mpi::Rank(mpi::COMM_WORLD), node_comm);
  mpi::Free(hash_comm);

  // Set up the list of model ranks on this node.
  int node_comm_size = mpi::Size(node_comm);
  for (int i = 0; i < node_comm_size; ++i) {
    model_ranks_on_node.push_back(mpi::Translate(node_comm, i, model_comm));
  }
}

uint8_t* lbann::lbann_comm::get_collective_buffer(size_t size, size_t idx) {
  auto buf_iter = collective_bufs.find(size);
  if (buf_iter == collective_bufs.end()) {
    if (idx != 0) {
      // TODO: Raise exception.
      return nullptr;
    }
    collective_bufs.emplace(std::make_pair(size, std::vector<uint8_t*>()));
    collective_bufs[size].push_back(new uint8_t[size]);
    return collective_bufs[size][0];
  } else {
    if (collective_bufs[size].size() > idx) {
      return collective_bufs[size][idx];
    } else {
      if (collective_bufs[size].size() != idx) {
        // TODO: Raise exception.
        return nullptr;
      }
      collective_bufs[size].push_back(new uint8_t[size]);
      return collective_bufs[size][idx];
    }
  }
}
