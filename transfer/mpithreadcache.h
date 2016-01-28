/**
 * @file
 *  This file is part of ASAGI.
 *
 *  ASAGI is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 3 of
 *  the License, or  (at your option) any later version.
 *
 *  ASAGI is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with ASAGI.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Diese Datei ist Teil von ASAGI.
 *
 *  ASAGI ist Freie Software: Sie koennen es unter den Bedingungen
 *  der GNU Lesser General Public License, wie von der Free Software
 *  Foundation, Version 3 der Lizenz oder (nach Ihrer Option) jeder
 *  spaeteren veroeffentlichten Version, weiterverbreiten und/oder
 *  modifizieren.
 *
 *  ASAGI wird in der Hoffnung, dass es nuetzlich sein wird, aber
 *  OHNE JEDE GEWAEHELEISTUNG, bereitgestellt; sogar ohne die implizite
 *  Gewaehrleistung der MARKTFAEHIGKEIT oder EIGNUNG FUER EINEN BESTIMMTEN
 *  ZWECK. Siehe die GNU Lesser General Public License fuer weitere Details.
 *
 *  Sie sollten eine Kopie der GNU Lesser General Public License zusammen
 *  mit diesem Programm erhalten haben. Wenn nicht, siehe
 *  <http://www.gnu.org/licenses/>.
 *
 * @copyright 2015-2016 Sebastian Rettenberger <rettenbs@in.tum.de>
 */

#ifndef TRANSFER_MPITHREADCACHE_H
#define TRANSFER_MPITHREADCACHE_H

#ifdef ASAGI_NOMPI
#include "mpino.h"
#else // ASAGI_NOMPI
#include "asagi.h"

#include <cassert>
#include <mutex>
#include <vector>

#include "utils/logger.h"

#include "mpicache.h"
#include "allocator/default.h"
#include "cache/cachemanager.h"
#include "mpi/commthread.h"
#include "mpi/scorephelper.h"
#include "threads/mutex.h"
#endif // ASAGI_NOMPI

namespace transfer
{

#ifdef ASAGI_NOMPI
/** No MPI transfers with MPI */
typedef MPINo MPIThreadCache;
#else // ASAGI_NOMPI

/**
 * Copies blocks between MPI processes assuming cache storage
 */
class MPIThreadCache : private MPICache<allocator::Default, false>
{
private:
	/**
	 * A class that response to block info requests
	 */
	class BlockInfoResponder : public mpi::Receiver
	{
	private:
		/** The parent transfer class */
		MPIThreadCache* m_parent;

	public:
		BlockInfoResponder()
			: m_parent(0L)
		{}

		/**
		 * Initialize the receiver
		 */
		void init(MPIThreadCache &parent)
		{
			m_parent = &parent;
		}

		void recv(int sender, unsigned long dictOffset)
		{
			int mpiResult; NDBG_UNUSED(mpiResult);

			long entry = m_parent->fetchBlockInfo(dictOffset);

			MPI_Request request;
			mpiResult = MPI_FUN(MPI_Isend)(&entry, 1, MPI_LONG, sender,
					m_parent->m_tagOffset+ENTRY_TAG,
					m_parent->mpiComm().comm(), &request);
			assert(mpiResult == MPI_SUCCESS);

			int done = 0;
			while (!done) {
				mpiResult = MPI_FUN(MPI_Test)(&request, &done, MPI_STATUS_IGNORE);
				assert(mpiResult == MPI_SUCCESS);
			}
		}
	};

	/**
	 * A class that transfers the block
	 */
	class BlockTransferer : public mpi::Receiver
	{
	private:
		/** The parent transfer class */
		MPIThreadCache* m_parent;

	public:
		BlockTransferer()
			: m_parent(0L)
		{}

		/**
		 * Initialize the receiver
		 */
		void init(MPIThreadCache &parent)
		{
			m_parent = &parent;
		}

		void recv(int sender, unsigned long blockId)
		{
			int mpiResult; NDBG_UNUSED(mpiResult);

			unsigned long cacheOffset;
			const unsigned char* data;

			// TODO for a large number of NUMA domains, this might be inefficient
			// In this case, it might be better to transfer the domainId and the blockId
			cache::CacheManager* cacheManager = 0L;
			for (unsigned int i = 0; i < m_parent->m_numaDomainSize; i++) {
				if (m_parent->m_cacheManager[i]->tryGet(blockId, cacheOffset, data)) {
					cacheManager = m_parent->m_cacheManager[i];
					break;
				}
			}

			if (cacheManager) {
				// Block found
				MPI_Request request;
				mpiResult = MPI_FUN(MPI_Isend)(const_cast<unsigned char*>(data),
						m_parent->m_blockSize, m_parent->m_mpiType,
						sender, m_parent->m_tagOffset+TRANSFER_TAG,
						m_parent->mpiComm().comm(), &request);
				assert(mpiResult == MPI_SUCCESS);

				int done = 0;
				while (!done) {
					mpiResult = MPI_FUN(MPI_Test)(&request, &done, MPI_STATUS_IGNORE);
					assert(mpiResult == MPI_SUCCESS);
				}

				cacheManager->unlock(cacheOffset);
			} else {
				// Block not found
				char ack;
				MPI_Request request;
				mpiResult = MPI_FUN(MPI_Isend)(&ack, 1, MPI_CHAR, sender,
						m_parent->m_tagOffset+TRANSFER_FAIL_TAG,
						m_parent->mpiComm().comm(), &request);
				assert(mpiResult == MPI_SUCCESS);

				int done = 0;
				while (!done) {
					mpiResult = MPI_FUN(MPI_Test)(&request, &done, MPI_STATUS_IGNORE);
					assert(mpiResult == MPI_SUCCESS);
				}
			}
		}
	};

	/**
	 * A class that can add blocks to the dictionary from a remote
	 * request
	 */
	class Adder : public mpi::Receiver
	{
	private:
		/** The parent transfer class */
		MPIThreadCache* m_parent;

	public:
		Adder()
			: m_parent(0L)
		{}

		/**
		 * Initialize the receiver
		 */
		void init(MPIThreadCache &parent)
		{
			m_parent = &parent;
		}

		void recv(int sender, unsigned long data)
		{
			// Extract the information
			unsigned long dictOffset = data / m_parent->rankCacheSize();
			unsigned long offset = data % m_parent->rankCacheSize();

			m_parent->addBlock(dictOffset, sender, offset);
		}
	};

	/**
	 * A class that can delete blocks from the dictionary
	 * from remote request.
	 */
	class Deleter : public mpi::Receiver
	{
	private:
		/** The parent transfer class */
		MPIThreadCache* m_parent;

	public:
		Deleter()
			: m_parent(0L)
		{}

		/**
		 * Initialize the receiver
		 */
		void init(MPIThreadCache &parent)
		{
			m_parent = &parent;
		}

		void recv(int sender, unsigned long data)
		{
			// Extract the information
			unsigned long dictOffset = data / m_parent->rankCacheSize();
			unsigned long offset = data % m_parent->rankCacheSize();

			m_parent->deleteBlock(dictOffset, sender, offset);
		}
	};

private:
	/** The NUMA domain ID */
	unsigned int m_numaDomainId;

	/** The total number of NUMA domains */
	unsigned int m_numaDomainSize;

	/** List of all cache managers */
	cache::CacheManager** m_cacheManager;

	/** A list of mutexes (one for each local block) */
	threads::Mutex* m_mutexes;

	/** Class responsible for responding to block info requests */
	BlockInfoResponder m_blockInfoResponder;

	/** Responder tag */
	int m_blockInfoTag;

	/** Class responsible for sending blocks */
	BlockTransferer m_blockTransferer;

	/** Transfer tag */
	int m_transfererTag;

	/** Class responsible for adding block info */
	Adder m_adder;

	/** Adder tag */
	int m_adderTag;

	/** Class responsible for deleting remote requests */
	Deleter m_deleter;

	/** Deleter tag */
	int m_deleterTag;

	/** Tag offset for responses */
	int m_tagOffset;

	/** Lock info send-receive pairs to avoid threading issues */
	threads::Mutex* m_infoMutex;

	/** Lock send-block send-receive pairs to avoid threading issues */
	threads::Mutex* m_blockMutex;

	/** Number of elements in one block */
	unsigned long m_blockSize;

	/** The type MPI type of an element */
	MPI_Datatype m_mpiType;

public:
	MPIThreadCache()
		: m_numaDomainId(0),
		  m_numaDomainSize(0),
		  m_cacheManager(0L),
		  m_mutexes(0L),
		  m_blockInfoTag(-1),
		  m_transfererTag(-1),
		  m_adderTag(-1),
		  m_deleterTag(-1),
		  m_tagOffset(0),
		  m_infoMutex(0L),
		  m_blockMutex(0L),
		  m_blockSize(0),
		  m_mpiType(MPI_DATATYPE_NULL)
	{
	}

	virtual ~MPIThreadCache()
	{
		if (m_numaDomainId == 0 && m_blockInfoTag >= 0) {
			delete [] m_cacheManager;

			mpi::CommThread::commThread.unregisterReceiver(m_blockInfoTag);
			mpi::CommThread::commThread.unregisterReceiver(m_transfererTag);
			mpi::CommThread::commThread.unregisterReceiver(m_adderTag);
			mpi::CommThread::commThread.unregisterReceiver(m_deleterTag);

			delete [] m_mutexes;
			delete m_infoMutex;
			delete m_blockMutex;
		}
	}

	/**
	 * Initialize the transfer class
	 *
	 * @param cache Pointer to the cache
	 * @param cacheSize Number of blocks in the cache
	 * @param cacheManager The cache manager
	 * @param blockCount Number local blocks
	 * @param blockSize Number of elements in one block
	 * @param type The data type of the elements
	 * @param mpiComm The MPI communicator
	 * @param numaComm The NUMA communicator
	 */
	asagi::Grid::Error init(unsigned char* cache,
			unsigned int cacheSize,
			cache::CacheManager &cacheManager,
			unsigned long blockCount,
			unsigned long blockSize,
			const types::Type &type,
			mpi::MPIComm &mpiComm,
			numa::NumaComm &numaComm)
	{
		m_numaDomainId = numaComm.domainId();
		m_numaDomainSize = numaComm.totalDomains();
		m_blockSize = blockSize;
		m_mpiType = type.getMPIType();

		MPICache<allocator::Default, false>::init(cacheSize,
				blockCount, mpiComm, numaComm);

		if (m_numaDomainId == 0) {
			m_cacheManager = new cache::CacheManager*[m_numaDomainSize];

			m_blockInfoResponder.init(*this);
			m_blockTransferer.init(*this);
			m_adder.init(*this);
			m_deleter.init(*this);

			// Create the mutexes for the blocks
			m_mutexes = new threads::Mutex[blockCount * m_numaDomainSize];

			// Initialize the receivers
			asagi::Grid::Error err = mpi::CommThread::commThread
				.registerReceiver(mpiComm.comm(), m_blockInfoResponder, m_blockInfoTag);
			if (err != asagi::Grid::SUCCESS)
				return err;

			err = mpi::CommThread::commThread
				.registerReceiver(mpiComm.comm(), m_blockTransferer, m_transfererTag);
			if (err != asagi::Grid::SUCCESS)
				return err;

			err = mpi::CommThread::commThread
				.registerReceiver(mpiComm.comm(), m_adder, m_adderTag);
			if (err != asagi::Grid::SUCCESS)
				return err;

			err = mpi::CommThread::commThread
				.registerReceiver(mpiComm.comm(), m_deleter, m_deleterTag);
			if (err != asagi::Grid::SUCCESS)
				return err;

			m_tagOffset = mpiComm.reserveTags(3);

			m_infoMutex = new threads::Mutex();
			m_blockMutex = new threads::Mutex();
		}

		asagi::Grid::Error err = numaComm.broadcast(m_cacheManager);
		if (err != asagi::Grid::SUCCESS)
			return err;

		m_cacheManager[m_numaDomainId] = &cacheManager;

		err = numaComm.broadcast(m_mutexes);
		if (err != asagi::Grid::SUCCESS)
			return err;

		err = numaComm.broadcast(m_blockInfoTag);
		if (err != asagi::Grid::SUCCESS)
			return err;

		err = numaComm.broadcast(m_transfererTag);
		if (err != asagi::Grid::SUCCESS)
			return err;

		err = numaComm.broadcast(m_adderTag);
		if (err != asagi::Grid::SUCCESS)
			return err;

		err = numaComm.broadcast(m_deleterTag);
		if (err != asagi::Grid::SUCCESS)
			return err;

		err = numaComm.broadcast(m_tagOffset);
		if (err != asagi::Grid::SUCCESS)
			return err;

		err = numaComm.broadcast(m_infoMutex);
		if (err != asagi::Grid::SUCCESS)
			return err;

		err = numaComm.broadcast(m_blockMutex);
		if (err != asagi::Grid::SUCCESS)
			return err;

		return asagi::Grid::SUCCESS;
	}

	/**
	 * Gets a possible remote storage for a blockId
	 *
	 * @param blockId The ID of the block that should be transfered
	 * @param dictRank The rank where the dictionary entry is stored
	 * @param dictOffset The offset of the dictionary entry on the rank
	 * @param offset The offset on the local rank where the block should be stored
	 * @return A dictionary entry that has to be passed to {@link transfer} if
	 *  the block should be fetched from a remote rank
	 */
	long startTransfer(unsigned long blockId, int dictRank,
			unsigned long dictOffset, unsigned long offset)
	{
		if (dictRank == mpiComm().rank())
			return fetchBlockInfo(dictOffset);

		// Ask remote process
		int mpiResult; NDBG_UNUSED(mpiResult);

		std::lock_guard<threads::Mutex> lock(*m_infoMutex);

		mpi::CommThread::commThread.send(m_blockInfoTag, dictRank, dictOffset);

		long entry;
		mpiResult = MPI_FUN(MPI_Recv)(&entry, 1, MPI_LONG, dictRank, ENTRY_TAG,
				mpiComm().comm(), MPI_STATUS_IGNORE);
		assert(mpiResult == MPI_SUCCESS);

		return entry;
	}

	/**
	 * Tries to transfers a block via MPI
	 *
	 * @param entry The dictionary entry obtained from {@link startTransfer}
	 * @param blockId The global id of the block
	 * @param cache Pointer to the local cache for this block
	 * @param[out] retry True of the transfer fails but MPI should be checked again
	 *  (with another entry)
	 * @return True if the block was fetched, false otherwise
	 */
	bool transfer(long entry, unsigned long blockId, unsigned char *cache, bool &retry)
	{
		if (entry < 0) {
			retry = false;
			return false;
		}

		int rank = entry / rankCacheSize();

		assert(rank >= 0);

		std::lock_guard<threads::Mutex> lock(*m_blockMutex);

		// Send the message to the remote rank
		mpi::CommThread::commThread.send(m_transfererTag, rank, blockId);

		int mpiResult; NDBG_UNUSED(mpiResult);
		MPI_Status status;
		int done = 0;
		while (!done) {
			// Try success full transfer first
			mpiResult = MPI_FUN(MPI_Iprobe)(rank, m_tagOffset+TRANSFER_TAG, mpiComm().comm(), &done, &status);
			assert(mpiResult == MPI_SUCCESS);

			if (!done) {
				// Try unsuccessfull transfer
				mpiResult = MPI_FUN(MPI_Iprobe)(rank, m_tagOffset+TRANSFER_FAIL_TAG, mpiComm().comm(),
					 &done, &status);
				assert(mpiResult == MPI_SUCCESS);
			}
		}

		if (status.MPI_TAG == m_tagOffset+TRANSFER_TAG) {
			// Success
			mpiResult = MPI_FUN(MPI_Recv)(cache, m_blockSize, m_mpiType, rank, m_tagOffset+TRANSFER_TAG,
					mpiComm().comm(), MPI_STATUS_IGNORE);
			assert(mpiResult == MPI_SUCCESS);

			retry = false;
			return true;
		}

		// Fail
		char ack;
		mpiResult = MPI_FUN(MPI_Recv)(&ack, 1, MPI_CHAR, rank, m_tagOffset+TRANSFER_FAIL_TAG,
				mpiComm().comm(), MPI_STATUS_IGNORE);
		assert(mpiResult == MPI_SUCCESS);

		retry = true;
		return false;
	}

	/**
	 * Ends a transfer phase started with {@link startTransfer}
	 *
	 * @param blockId The ID of the block that was transfered
	 * @param dictRank The rank where the dictionary entry is stored
	 * @param dictOffset The offset of the dictionary entry on the rank
	 * @param offset The offset on the local rank where the block should be stored
	 */
	void endTransfer(unsigned long blockId, int dictRank,
			unsigned long dictOffset, unsigned long offset)
	{
		addBlock(blockId, dictRank, dictOffset, offset);
	}

	/**
	 * Adds information about a local stored block
	 *
	 * @param blockId The ID of the block that should be transfered
	 * @param dictRank The rank where the dictionary entry is stored
	 * @param dictOffset The offset of the dictionary entry on the rank
	 * @param offset The offset on the local rank where the block should be stored
	 * @return A dictionary entry that has to be passed to {@link transfer} if
	 *  the block should be fetched from a remote rank
	 */
	void addBlock(unsigned long blockId, int dictRank,
			unsigned long dictOffset, unsigned long offset)
	{
		if (dictRank == mpiComm().rank())
			addBlock(dictOffset, dictRank, offset);
		else {
			// Assemble information
			assert(offset < rankCacheSize());
			unsigned long data = dictOffset * rankCacheSize() + offset;

			mpi::CommThread::commThread.send(m_adderTag, dictRank, data);
		}
	}

	/**
	 * Deletes information about a local stored block
	 *
	 * @param blockId The global block id
	 * @param dictRank The rank where the dictionary entry is stored
	 * @param dictOffset The offset of the dictionary entry on the rank
	 * @param offset The offset on the local rank
	 */
	void deleteBlock(long blockId, int dictRank, unsigned long dictOffset,
			unsigned long offset)
	{
		if (blockId < 0)
			// Invalid block id
			return;

		if (dictRank == mpiComm().rank())
			deleteBlock(dictOffset, dictRank, offset);
		else {
			// Assemble information
			assert(offset < rankCacheSize());
			unsigned long data = dictOffset * rankCacheSize() + offset;

			mpi::CommThread::commThread.send(m_deleterTag, dictRank, data);
		}
	}

private:
	/**
	 * Retrieves a global offset of the block
	 *
	 * @param dictOffset The local id of the block
	 * @return The global offset of a storage or -1 if the block is not
	 *  in any cache
	 */
	long fetchBlockInfo(unsigned long dictOffset)
	{
		std::lock_guard<threads::Mutex> lock(m_mutexes[dictOffset]);

		return MPICache::fetchBlockInfo(dictionary(dictOffset));
	}

	/**
	 * Adds a block entry to the dictionary
	 *
	 * @param dictOffset The offset in the local dictionary
	 * @param rank The rank where the block was
	 * @param offset The offset of the block on this rank
	 */
	void addBlock(unsigned long dictOffset, int rank, unsigned long offset)
	{
		std::lock_guard<threads::Mutex> lock(m_mutexes[dictOffset]);

		updateBlockInfo(dictionary(dictOffset), rank * rankCacheSize() + offset);
	}

	/**
	 * Delete a block entry from the dictionary
	 *
	 * @param dictOffset The offset in the local dictionary
	 * @param rank The rank where the block was
	 * @param offset The offset of the block on this rank
	 */
	void deleteBlock(unsigned long dictOffset, int rank, unsigned long offset)
	{
		std::lock_guard<threads::Mutex> lock(m_mutexes[dictOffset]);

		deleteBlockInfo(dictionary(dictOffset), rank * rankCacheSize() + offset);
	}

private:
	/** Tag for return entry value */
	static const int ENTRY_TAG = 0;

	/** Tag for successful transfer */
	static const int TRANSFER_TAG = 1;

	/** Tag for unsuccessful transfer */
	static const int TRANSFER_FAIL_TAG = 2;
};

#endif // ASAGI_NOMPI

}

#endif // TRANSFER_MPITHREADCACHE_H

