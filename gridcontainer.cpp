#include "gridcontainer.h"

#include "nompigrid.h"
#ifndef ASAGI_NOMPI
#include "simplegrid.h"
#include "largegrid.h"
#endif // ASAGI_NOMPI

#include "types/arraytype.h"
#include "types/basictype.h"

#include <cassert>
#include <cstring>
#include <limits>

GridContainer::GridContainer(Type type, bool isArray, unsigned int hint,
	unsigned int levels)
{
	assert(levels > 0); // 0 levels don't make sense
	
	// Prepare for fortran <-> c translation
	m_id = m_pointers.add(this);
	
	m_levels = levels;
	
#ifndef ASAGI_NOMPI
	m_communicator = MPI_COMM_NULL;
#endif // ASAGI_NOMPI
	
	if (isArray) {
		switch (type) {
		case BYTE:
			m_type = new types::ArrayType<char>();
			break;
		case INT:
			m_type = new types::ArrayType<int>();
			break;
		case LONG:
			m_type = new types::ArrayType<long>();
			break;
		case FLOAT:
			m_type = new types::ArrayType<float>();
			break;
		case DOUBLE:
			m_type = new types::ArrayType<double>();
			break;
		default:
			m_type = 0L;
			assert(false);
		}
	} else {
		switch (type) {
		case BYTE:
			m_type = new types::BasicType<char>();
			break;
		case INT:
			m_type = new types::BasicType<int>();
			break;
		case LONG:
			m_type = new types::BasicType<long>();
			break;
		case FLOAT:
			m_type = new types::BasicType<float>();
			break;
		case DOUBLE:
			m_type = new types::BasicType<double>();
			break;
		default:
			m_type = 0L;
			assert(false);
		}
	}
	
	m_minX = m_minY = m_minZ = -std::numeric_limits<double>::infinity();
	m_maxX = m_maxY = m_maxZ = std::numeric_limits<double>::infinity();
	
	m_valuePos = CELL_CENTERED;
	
	m_grids = new ::Grid*[levels];
#ifdef ASAGI_NOMPI
	for (unsigned int i = 0; i < levels; i++)
		m_grids[i] = new NoMPIGrid(*this, hint);
#else // ASAGI_NOMPI
	if (hint & asagi::LARGE_GRID) {
		for (unsigned int i = 0; i < levels; i++)
			m_grids[i] = new LargeGrid(*this, hint, i);
	} else {
		for (unsigned int i = 0; i < levels; i++)
			m_grids[i] = new SimpleGrid(*this, hint);
	}
#endif // ASAGI_NOMPI
	
	// Default values (probably only useful when compiled without MPI support)
	m_mpiRank = 0;
	m_mpiSize = 1;
}


GridContainer::~GridContainer()
{
	for (unsigned int i = 0; i < m_levels; i++)
		delete m_grids[i];
	delete [] m_grids;
	delete m_type;
	
#ifndef ASAGI_NOMPI
	if (m_communicator != MPI_COMM_NULL)
		MPI_Comm_free(&m_communicator);
#endif // ASAGI_NOMPI
	
	// Remove from fortran <-> c translation
	m_pointers.remove(m_id);
}

#ifndef ASAGI_NOMPI
asagi::Grid::Error GridContainer::setComm(MPI_Comm comm)
{
	if (m_communicator != MPI_COMM_NULL)
		// set communicator only once
		return SUCCESS;
	
	if (MPI_Comm_dup(comm, &m_communicator) != MPI_SUCCESS)
		return MPI_ERROR;
	
	MPI_Comm_rank(m_communicator, &m_mpiRank);
	MPI_Comm_size(m_communicator, &m_mpiSize);
	
	return SUCCESS;
}
#endif // ASAGI_NOMPI

asagi::Grid::Error GridContainer::setParam(const char* name, const char* value,
	unsigned int level)
{
	if (strcmp(name, "value-position") == 0) {
		if (strcmp(value, "cell-centered") == 0) {
			m_valuePos = CELL_CENTERED;
			return SUCCESS;
		}
		
		if (strcmp(value, "vertex-centered") == 0) {
			m_valuePos = VERTEX_CENTERED;
			return SUCCESS;
		}
		
		return INVALID_VALUE;
	}
	
	assert(level < m_levels);
	return m_grids[level]->setParam(name, value);
}

asagi::Grid::Error GridContainer::open(const char* filename, unsigned int level)
{
	Error result;
	
	assert(level < m_levels);
	
#ifndef ASAGI_NOMPI
	result = setComm(); // Make sure we have our own communicator
	if (result != SUCCESS)
		return result;
#endif // ASAGI_NOMPI
	
	result = m_grids[level]->open(filename);
	if (result != SUCCESS)
		return result;
	
	m_minX = std::max(m_minX, m_grids[level]->getXMin());
	m_minY = std::max(m_minY, m_grids[level]->getYMin());
	m_minZ = std::max(m_minZ, m_grids[level]->getZMin());
	
	m_maxX = std::min(m_maxX, m_grids[level]->getXMax());
	m_maxY = std::min(m_maxY, m_grids[level]->getYMax());
	m_maxZ = std::min(m_maxZ, m_grids[level]->getZMax());
	
	return result;
}

char GridContainer::getByte3D(double x, double y, double z, unsigned int level)
{
	assert(level < m_levels);
	
	return m_grids[level]->getByte(x, y, z);
}

int GridContainer::getInt3D(double x, double y, double z, unsigned int level)
{
	assert(level < m_levels);
	
	return m_grids[level]->getInt(x, y, z);
}

long GridContainer::getLong3D(double x, double y, double z,
	unsigned int level)
{
	assert(level < m_levels);
	
	return m_grids[level]->getLong(x, y, z);
}

float GridContainer::getFloat3D(double x, double y, double z,
	unsigned int level)
{
	assert(level < m_levels);
	
	return m_grids[level]->getFloat(x, y, z);
}

double GridContainer::getDouble3D(double x, double y, double z,
	unsigned int level)
{
	assert(level < m_levels);
	
	return m_grids[level]->getDouble(x, y, z);
}

void GridContainer::getBuf3D(void* buf, double x, double y, double z,
	unsigned int level)
{
	assert(level < m_levels);
	
	m_grids[level]->getBuf(buf, x, y, z);
}

bool GridContainer::exportPng(const char* filename, unsigned int level)
{
	assert(level < m_levels);
	
	return m_grids[level]->exportPng(filename);
}

// Fortran <-> c translation array
fortran::PointerArray<GridContainer> GridContainer::m_pointers;
