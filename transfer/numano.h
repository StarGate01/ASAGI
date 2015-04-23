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
 * @copyright 2015 Sebastian Rettenberger <rettenbs@in.tum.de>
 */

#ifndef TRANSFER_NUMANO_H
#define TRANSFER_NUMANO_H

namespace transfer
{

/**
 * Disables all NUMA copies
 */
class NumaNo
{
private:

public:
	NumaNo()
	{
	}

	virtual ~NumaNo()
	{
	}

	/**
	 * Initialize the transfer class
	 *
	 * Stub for {@link NumaFull::init}
	 */
	asagi::Grid::Error init(const unsigned char* data,
			unsigned long blockCount,
			unsigned long blockSize,
			const types::Type &type,
			const mpi::MPIComm &mpiComm,
			const numa::NumaComm &numaComm,
			cache::CacheManager &cacheManager)
	{
		return asagi::Grid::SUCCESS;
	}

	/**
	 * Initialize the transfer class
	 *
	 * Stub for {@link NumaCache::init}
	 */
	asagi::Grid::Error init(unsigned long blockSize,
			const types::Type &type,
			numa::NumaComm &numaComm,
			cache::CacheManager &cacheManager)
	{
		return asagi::Grid::SUCCESS;
	}

	/**
	 * Stub for {@link NumaFull::transfer}
	 *
	 * @return Always false
	 */
	bool transfer(unsigned long blockId,
			int remoteRank, unsigned int domainId, unsigned long offset,
			unsigned char *cache)
	{
		return false;
	}

	/**
	 * Stub for {@link NumaCache::transfer}
	 *
	 * @return Always false
	 */
	bool transfer(unsigned long blockId,
			unsigned char* cache)
	{
		return false;
	}
};

}

#endif // TRANSFER_NUMANO_H

