#include "gamecontext.h"
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <game/gamecore.h>
#include <generated/protocol.h>
#include <game/layers.h>
#include <game/mapitems.h>
#include <sqlite3.h>
#include <cstdlib>
#include <algorithm>
#include <limits>
#include <optional>
#include <queue>
#include <vector>

#include "bot.h"
#include "botengine.h"

static bool IsSupportedBotLearningMap(const char *pName)
{
	return pName && (str_comp(pName, "TeeTycoon") == 0 || str_comp(pName, "Copy Love Box-TT") == 0);
}

CGraph::CGraph()
{
	m_NumVertices = 0;
	m_NumEdges = 0;
	m_pEdges = 0;
	m_pVertices = 0;
	m_pClosestPath = 0;
	m_Diameter = 0;
}
CGraph::~CGraph()
{
	Free();
}
void CGraph::Reset()
{
	Free();
	m_NumVertices = 0;
	m_NumEdges = 0;
	m_Diameter = 0;
}
void CGraph::Free()
{
	if(m_pEdges)
		std::free(m_pEdges);
	if(m_pVertices)
		std::free(m_pVertices);
	if(m_pClosestPath)
		std::free(m_pClosestPath);
	m_pVertices = 0;
	m_pEdges = 0;
	m_pClosestPath = 0;
}

void CGraph::ComputeClosestPath()
{
	if(m_NumVertices <= 0 || !m_pVertices || (m_NumEdges > 0 && !m_pEdges))
		return;
	if(m_pClosestPath)
		std::free(m_pClosestPath);
	const size_t MatrixSize = static_cast<size_t>(m_NumVertices) * m_NumVertices;
	m_pClosestPath = static_cast<int *>(std::malloc(MatrixSize * sizeof(int)));
	if(!m_pClosestPath)
		return;
	std::fill(m_pClosestPath, m_pClosestPath + MatrixSize, -1);
	std::vector<std::vector<int>> Adjacency(m_NumVertices);
	for(int i = 0; i < m_NumEdges; i++)
	{
		const CEdge &Edge = m_pEdges[i];
		if(Edge.m_StartID < 0 || Edge.m_StartID >= m_NumVertices || Edge.m_EndID < 0 || Edge.m_EndID >= m_NumVertices)
			continue;
		Adjacency[Edge.m_StartID].push_back(Edge.m_EndID);
	}

	m_Diameter = 1;
	std::vector<int> Dist(m_NumVertices);
	std::queue<int> Queue;
	for(int Source = 0; Source < m_NumVertices; Source++)
	{
		std::fill(Dist.begin(), Dist.end(), -1);
		while(!Queue.empty())
			Queue.pop();
		Dist[Source] = 0;
		Queue.push(Source);
		while(!Queue.empty())
		{
			const int Current = Queue.front();
			Queue.pop();
			for(const int Next : Adjacency[Current])
			{
				if(Dist[Next] >= 0)
					continue;
				Dist[Next] = Dist[Current] + 1;
				m_pClosestPath[Source + Next * m_NumVertices] = Current == Source ? Next : m_pClosestPath[Source + Current * m_NumVertices];
				m_Diameter = std::max(m_Diameter, Dist[Next]);
				Queue.push(Next);
			}
		}
	}
	dbg_msg("botengine", "closest path computed, diameter=%d", m_Diameter);
}

int CGraph::GetPath(int Start, int End, vec2 *pVertices)
{
	Start = std::clamp(Start, 0, m_NumVertices - 1);
	End = std::clamp(End, 0, m_NumVertices - 1);
	if(m_pClosestPath[Start + End * m_NumVertices] < 0)
		return 0;
	int Size = 0;
	int Cur = Start;
	while(Cur != End)
	{
		pVertices[Size++] = m_pVertices[Cur].m_Pos;
		Cur = m_pClosestPath[Cur + End * m_NumVertices];
	}
	pVertices[Size++] = m_pVertices[Cur].m_Pos;

	return Size;
}

CBotEngine::CBotEngine(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pGrid = 0;
	m_Triangulation.m_pTriangles = 0;
	m_Triangulation.m_Size = 0;
	m_pCorners = 0;
	m_CornerCount = 0;
	m_pSegments = 0;
	m_SegmentCount = 0;
	mem_zero(m_aPaths, sizeof(m_aPaths));
	mem_zero(m_apBot, sizeof(m_apBot));
	mem_zero(m_aBotTrails, sizeof(m_aBotTrails));
	for(auto &Trail : m_aBotTrails)
	{
		Trail.m_LastTile = -1;
		Trail.m_LastTileTick = -1;
	}
}

void CBotEngine::Free()
{
	m_vTeleEntrances.clear();
	if(m_pGrid)
		std::free(m_pGrid);
	m_pGrid = nullptr;

	if(m_Triangulation.m_pTriangles)
		std::free(m_Triangulation.m_pTriangles);
	m_Triangulation.m_Size = 0;
	m_Triangulation.m_pTriangles = 0;

	if(m_pCorners)
		std::free(m_pCorners);
	m_CornerCount = 0;
	m_pCorners = 0;

	for(int k = 0; k < m_SegmentCount; k++)
	{
		CSegment *pSegment = m_pSegments + k;
		if(pSegment->m_SnapID >= 0)
			GameServer()->Server()->SnapFreeId(pSegment->m_SnapID);
	}
	if(m_pSegments)
		std::free(m_pSegments);
	m_SegmentCount = 0;
	m_pSegments = 0;

	m_Graph.Free();
	m_Graph.m_NumEdges = 0;
	m_Graph.m_NumVertices = 0;
	m_GraphInitialized = false;

	for(int c = 0; c < MAX_CLIENTS; c++)
	{
		if(m_aPaths[c].m_pVertices)
			std::free(m_aPaths[c].m_pVertices);
		if(m_aPaths[c].m_pSnapID)
		{
			for(int i = 0; i < m_aPaths[c].m_MaxSize; i++)
				if(m_aPaths[c].m_pSnapID[i] >= 0)
					GameServer()->Server()->SnapFreeId(m_aPaths[c].m_pSnapID[i]);
			std::free(m_aPaths[c].m_pSnapID);
		}
	}
	mem_zero(m_aPaths, sizeof(m_aPaths));
}

CBotEngine::~CBotEngine()
{
	Free();
}

void CBotEngine::Init(CTile *pTiles, int Width, int Height)
{
	m_pTiles = pTiles;

	m_Width = Width;
	m_Height = Height;
	m_vNavigationDeaths.assign(static_cast<size_t>(Width) * Height, 0);
	m_vNavigationBestTicks.assign(static_cast<size_t>(Width) * Height, std::numeric_limits<int>::max());

	Free();
	for(auto &Trail : m_aBotTrails)
	{
		mem_zero(&Trail, sizeof(Trail));
		Trail.m_LastTile = -1;
		Trail.m_LastTileTick = -1;
	}
	if(GameServer()->db && GameServer()->Map() && IsSupportedBotLearningMap(GameServer()->Map()->BaseName()))
	{
		sqlite3_stmt *pStmt = nullptr;
		if(sqlite3_prepare_v2(GameServer()->db, "SELECT TILE_INDEX, DEATHS FROM TT_BOT_NAV_LEARNING WHERE MAP_NAME = ?1", -1, &pStmt, nullptr) == SQLITE_OK)
		{
			sqlite3_bind_text(pStmt, 1, GameServer()->Map()->BaseName(), -1, SQLITE_TRANSIENT);
			while(sqlite3_step(pStmt) == SQLITE_ROW)
			{
				const int Tile = sqlite3_column_int(pStmt, 0);
				const int Deaths = sqlite3_column_int(pStmt, 1);
				if(Tile >= 0 && Tile < static_cast<int>(m_vNavigationDeaths.size()))
					m_vNavigationDeaths[Tile] = static_cast<uint8_t>(std::clamp(Deaths, 0, 255));
			}
		}
		sqlite3_finalize(pStmt);
		if(sqlite3_prepare_v2(GameServer()->db, "SELECT TILE_INDEX, BEST_TICKS FROM TT_BOT_NAV_SPEED WHERE MAP_NAME = ?1", -1, &pStmt, nullptr) == SQLITE_OK)
		{
			sqlite3_bind_text(pStmt, 1, GameServer()->Map()->BaseName(), -1, SQLITE_TRANSIENT);
			while(sqlite3_step(pStmt) == SQLITE_ROW)
			{
				const int Tile = sqlite3_column_int(pStmt, 0);
				const int BestTicks = sqlite3_column_int(pStmt, 1);
				if(Tile >= 0 && Tile < static_cast<int>(m_vNavigationBestTicks.size()))
					m_vNavigationBestTicks[Tile] = std::max(1, BestTicks);
			}
		}
		sqlite3_finalize(pStmt);
	}
	if(GameServer()->Collision()->TeleLayer())
	{
		for(int Index = 0; Index < Width * Height; ++Index)
		{
			int Number = GameServer()->Collision()->IsTeleport(Index);
			if(!Number)
				Number = GameServer()->Collision()->IsEvilTeleport(Index);
			if(Number > 0 && !GameServer()->Collision()->TeleOuts(Number - 1).empty())
				m_vTeleEntrances.push_back({ConvertIndex(Index), Number});
		}
	}

	m_pGrid = (int *)std::malloc(m_Width * m_Height * sizeof(int));
	if(m_pGrid)
	{
		mem_zero(m_pGrid, m_Width * m_Height * sizeof(int));

		int j = m_Height - 1;
		int Margin = 6;
		for(int i = 0; i < m_Width; i++)
		{
			int Index = m_pTiles[i + j * m_Width].m_Index;

			if(Index <= TILE_NOHOOK)
			{
				m_pGrid[i + j * m_Width] = Index + GTILE_AIR - TILE_AIR;
			}
			if(Index >= ENTITY_OFFSET + ENTITY_FLAGSTAND_RED && Index <= ENTITY_OFFSET + ENTITY_FLAGSTAND_BLUE)
			{
				m_pGrid[i + j * m_Width] = Index + GTILE_FLAGSTAND_RED - ENTITY_FLAGSTAND_RED - ENTITY_OFFSET;
				if(m_pGrid[i + j * m_Width] <= GTILE_FLAGSTAND_BLUE)
					m_aFlagStandPos[m_pGrid[i + j * m_Width]] = vec2(i, j) * 32 + vec2(16, 16);
			}

			if(m_pGrid[i + j * m_Width] == GTILE_SOLID || m_pGrid[i + j * m_Width] == GTILE_NOHOOK)
				m_pGrid[i + j * m_Width] |= BTILE_SAFE;
			else
				m_pGrid[i + j * m_Width] |= BTILE_HOLE;
		}
		for(int i = Margin; i < m_Width; i++)
		{
			if(m_pGrid[i + j * m_Width] & BTILE_SAFE)
				break;
			else
				m_pGrid[i - Margin + j * m_Width] |= BTILE_LHOLE;
		}
		for(int i = m_Width - 1; i >= Margin; i--)
		{
			if(m_pGrid[i - Margin + j * m_Width] & BTILE_SAFE)
				break;
			else
				m_pGrid[i + j * m_Width] |= BTILE_RHOLE;
		}
		for(int j = m_Height - 2; j >= 0; j--)
		{
			for(int i = 0; i < m_Width; i++)
			{
				int Index = m_pTiles[i + j * m_Width].m_Index;

				if(Index <= TILE_NOHOOK)
				{
					m_pGrid[i + j * m_Width] = Index + GTILE_AIR - TILE_AIR;
				}
				else if(Index >= ENTITY_OFFSET + ENTITY_FLAGSTAND_RED && Index <= ENTITY_OFFSET + ENTITY_FLAGSTAND_BLUE)
				{
					m_pGrid[i + j * m_Width] = Index + GTILE_FLAGSTAND_RED - ENTITY_FLAGSTAND_RED - ENTITY_OFFSET;
					if(m_pGrid[i + j * m_Width] <= GTILE_FLAGSTAND_BLUE)
						m_aFlagStandPos[m_pGrid[i + j * m_Width]] = vec2(i, j) * 32 + vec2(16, 16);
				}
				else
				{
					m_pGrid[i + j * m_Width] = GTILE_AIR;
				}

				if(m_pGrid[i + j * m_Width] == GTILE_SOLID || m_pGrid[i + j * m_Width] == GTILE_NOHOOK)
					m_pGrid[i + j * m_Width] |= BTILE_SAFE;
				else if(m_pGrid[i + j * m_Width] == GTILE_DEATH)
					m_pGrid[i + j * m_Width] |= BTILE_HOLE;
				else
				{
					bool Left = i > 0 && m_pGrid[i - 1 + (j + 1) * m_Width] & BTILE_SAFE && !(m_pGrid[i + (j + 1) * m_Width] & BTILE_RHOLE) && (m_pGrid[i - 1 + (j + 1) * m_Width] & GTILE_MASK) <= GTILE_AIR;
					bool Right = i < m_Width - 1 && m_pGrid[i + 1 + (j + 1) * m_Width] & BTILE_SAFE && !(m_pGrid[i + (j + 1) * m_Width] & BTILE_LHOLE) && (m_pGrid[i + 1 + (j + 1) * m_Width] & GTILE_MASK) <= GTILE_AIR;

					if(Left || Right || m_pGrid[i + (j + 1) * m_Width] & BTILE_SAFE)
						m_pGrid[i + j * m_Width] |= BTILE_SAFE;
					else
						m_pGrid[i + j * m_Width] |= BTILE_HOLE;
				}
			}
			for(int i = Margin; i < m_Width; i++)
			{
				if(!((m_pGrid[i + j * m_Width] & GTILE_MASK) <= GTILE_AIR))
					break;
				else
					m_pGrid[i - Margin + j * m_Width] |= BTILE_LHOLE;
			}
			for(int i = m_Width - 1; i >= Margin; i--)
			{
				if(!((m_pGrid[i - Margin + j * m_Width] & GTILE_MASK) <= GTILE_AIR))
					break;
				else
					m_pGrid[i + j * m_Width] |= BTILE_RHOLE;
			}
		}

		GenerateCorners();
		GenerateSegments();
	}
}

bool CBotEngine::InitGraph()
{
	if(m_GraphInitialized)
		return true;
	if(!m_pGrid)
		return false;

	GenerateTriangles();
	GenerateGraphFromTriangles();
	if(!m_Graph.m_pClosestPath || m_Graph.m_NumVertices <= 0)
		return false;

	m_GraphInitialized = true;
	return true;
}

bool CBotEngine::InitPath(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_GraphInitialized)
		return false;

	CPath &Path = m_aPaths[ClientId];
	if(Path.m_pVertices && Path.m_pSnapID)
		return true;

	Path.m_MaxSize = m_Graph.m_Diameter + 2;
	Path.m_pVertices = static_cast<vec2 *>(std::malloc(Path.m_MaxSize * sizeof(vec2)));
	Path.m_pSnapID = static_cast<int *>(std::malloc(Path.m_MaxSize * sizeof(int)));
	if(!Path.m_pVertices || !Path.m_pSnapID)
	{
		std::free(Path.m_pVertices);
		std::free(Path.m_pSnapID);
		Path.m_pVertices = nullptr;
		Path.m_pSnapID = nullptr;
		Path.m_MaxSize = 0;
		return false;
	}
	for(int i = 0; i < Path.m_MaxSize; i++)
	{
		const std::optional<int> SnapID = GameServer()->Server()->SnapNewId();
		if(!SnapID)
		{
			for(int j = 0; j < i; j++)
				if(Path.m_pSnapID[j] >= 0)
					GameServer()->Server()->SnapFreeId(Path.m_pSnapID[j]);
			std::free(Path.m_pVertices);
			std::free(Path.m_pSnapID);
			Path.m_pVertices = nullptr;
			Path.m_pSnapID = nullptr;
			Path.m_MaxSize = 0;
			return false;
		}
		Path.m_pSnapID[i] = *SnapID;
	}
	Path.m_Size = 0;
	return true;
}

bool CBotEngine::InitializeForBot(int ClientId)
{
	return InitGraph() && InitPath(ClientId);
}

void CBotEngine::GenerateSegments()
{
	int VSegmentCount = 0;
	int HSegmentCount = 0;
	// Vertical segments
	for(int i = 1; i < m_Width - 1; i++)
	{
		bool right = false;
		bool left = false;
		for(int j = 0; j < m_Height; j++)
		{
			if((m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i + 1 + j * m_Width] & GTILE_MASK) <= GTILE_AIR)
				left = true;
			else if(left)
			{
				VSegmentCount++;
				left = false;
			}
			if((m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i - 1 + j * m_Width] & GTILE_MASK) <= GTILE_AIR)
				right = true;
			else if(right)
			{
				VSegmentCount++;
				right = false;
			}
		}
		if(left)
			VSegmentCount++;
		if(right)
			VSegmentCount++;
	}
	// Horizontal segments
	for(int j = 1; j < m_Height - 1; j++)
	{
		bool up = false;
		bool down = false;
		for(int i = 0; i < m_Width; i++)
		{
			if(up && ((m_pGrid[i + j * m_Width] & GTILE_MASK) <= GTILE_AIR || (m_pGrid[i + (j + 1) * m_Width] & GTILE_MASK) > GTILE_AIR))
			{
				HSegmentCount++;
				up = false;
			}
			if(down && ((m_pGrid[i + j * m_Width] & GTILE_MASK) <= GTILE_AIR || (m_pGrid[i + (j - 1) * m_Width] & GTILE_MASK) > GTILE_AIR))
			{
				HSegmentCount++;
				down = false;
			}

			if(!up && (m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i + (j + 1) * m_Width] & GTILE_MASK) <= GTILE_AIR)
				up = true;
			if(!down && (m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i + (j - 1) * m_Width] & GTILE_MASK) <= GTILE_AIR)
				down = true;
		}
		if(up)
			HSegmentCount++;
		if(down)
			HSegmentCount++;
	}
	dbg_msg("botengine", "Found %d vertical segments, and %d horizontal segments", VSegmentCount, HSegmentCount);
	m_SegmentCount = HSegmentCount + VSegmentCount;
	m_HSegmentCount = HSegmentCount;
	m_pSegments = (CSegment *)std::malloc(m_SegmentCount * sizeof(CSegment));
	CSegment *pSegment = m_pSegments;
	// Horizontal segments
	for(int j = 1; j < m_Height - 1; j++)
	{
		bool up = false;
		bool down = false;
		int up_i = 1, down_i = 1;
		for(int i = 0; i < m_Width; i++)
		{
			if((m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i + (j + 1) * m_Width] & GTILE_MASK) <= GTILE_AIR)
			{
				if(!up)
					up_i = i;
				up = true;
			}
			else if(up)
			{
				pSegment->m_IsVertical = false;
				if(!up_i)
					up_i = -200;
				pSegment->m_A = vec2(up_i * 32, (j + 1) * 32);
				pSegment->m_B = vec2(i * 32, (j + 1) * 32);
				pSegment->m_SnapID = GameServer()->Server()->SnapNewId().value_or(-1);
				pSegment++;
				up = false;
			}

			if((m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i + (j - 1) * m_Width] & GTILE_MASK) <= GTILE_AIR)
			{
				if(!down)
					down_i = i;
				down = true;
			}
			else if(down)
			{
				pSegment->m_IsVertical = false;
				if(!down_i)
					down_i = -200;
				pSegment->m_A = vec2(down_i * 32, j * 32);
				pSegment->m_B = vec2(i * 32, j * 32);
				pSegment->m_SnapID = GameServer()->Server()->SnapNewId().value_or(-1);
				pSegment++;
				down = false;
			}
		}
		if(up)
		{
			pSegment->m_IsVertical = false;
			if(!up_i)
				up_i = -200;
			pSegment->m_A = vec2(up_i * 32, (j + 1) * 32);
			pSegment->m_B = vec2((m_Width + 200) * 32, (j + 1) * 32);
			pSegment->m_SnapID = GameServer()->Server()->SnapNewId().value_or(-1);
			pSegment++;
			up = false;
		}
		if(down)
		{
			pSegment->m_IsVertical = false;
			if(!down_i)
				down_i = -200;
			pSegment->m_A = vec2(down_i * 32, j * 32);
			pSegment->m_B = vec2((m_Width + 200) * 32, j * 32);
			pSegment->m_SnapID = GameServer()->Server()->SnapNewId().value_or(-1);
			pSegment++;
			down = false;
		}
	}
	// Vertical segments
	for(int i = 1; i < m_Width - 1; i++)
	{
		bool right = false;
		bool left = false;
		int right_j = 1, left_j = 1;
		for(int j = 0; j < m_Height; j++)
		{
			if((m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i + 1 + j * m_Width] & GTILE_MASK) <= GTILE_AIR)
			{
				if(!left)
					left_j = j;
				left = true;
			}
			else if(left)
			{
				pSegment->m_IsVertical = true;
				if(!left_j)
					left_j = -200;
				pSegment->m_A = vec2((i + 1) * 32, left_j * 32);
				pSegment->m_B = vec2((i + 1) * 32, j * 32);
				pSegment->m_SnapID = GameServer()->Server()->SnapNewId().value_or(-1);
				pSegment++;
				left = false;
			}
			if((m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i - 1 + j * m_Width] & GTILE_MASK) <= GTILE_AIR)
			{
				if(!right)
					right_j = j;
				right = true;
			}
			else if(right)
			{
				pSegment->m_IsVertical = true;
				if(!right_j)
					right_j = -200;
				pSegment->m_A = vec2(i * 32, right_j * 32);
				pSegment->m_B = vec2(i * 32, j * 32);
				pSegment->m_SnapID = GameServer()->Server()->SnapNewId().value_or(-1);
				pSegment++;
				right = false;
			}
		}
		if(left)
		{
			pSegment->m_IsVertical = true;
			if(!left_j)
				left_j = -200;
			pSegment->m_A = vec2((i + 1) * 32, left_j * 32);
			pSegment->m_B = vec2((i + 1) * 32, (m_Height + 200) * 32);
			pSegment->m_SnapID = GameServer()->Server()->SnapNewId().value_or(-1);
			pSegment++;
			left = false;
		}
		if(right)
		{
			pSegment->m_IsVertical = true;
			if(!right_j)
				right_j = -200;
			pSegment->m_A = vec2(i * 32, right_j * 32);
			pSegment->m_B = vec2(i * 32, (m_Height + 200) * 32);
			pSegment->m_SnapID = GameServer()->Server()->SnapNewId().value_or(-1);
			pSegment++;
			right = false;
		}
	}
	dbg_msg("botengine", "Allocate %d segments, use %d", m_SegmentCount, pSegment - m_pSegments);
	if(m_SegmentCount != pSegment - m_pSegments)
		exit(1);
	qsort(m_pSegments, HSegmentCount, sizeof(CSegment), SegmentComp);
	qsort(m_pSegments + HSegmentCount, VSegmentCount, sizeof(CSegment), SegmentComp);
}

int CBotEngine::SegmentComp(const void *a, const void *b)
{
	CSegment *s0 = (CSegment *)a;
	CSegment *s1 = (CSegment *)b;
	if(s0->m_IsVertical && !s1->m_IsVertical)
		return 1;
	if(!s0->m_IsVertical && s1->m_IsVertical)
		return -1;
	if(s0->m_IsVertical)
	{
		if(s0->m_A.x < s1->m_A.x)
			return -1;
		if(s0->m_A.x > s1->m_A.x)
			return 1;
		if(s0->m_A.y < s1->m_A.y)
			return -1;
		if(s0->m_A.y > s1->m_A.y)
			return 1;
		return 0;
	}
	if(s0->m_A.y < s1->m_A.y)
		return -1;
	if(s0->m_A.y > s1->m_A.y)
		return 1;
	if(s0->m_A.x < s1->m_A.x)
		return -1;
	if(s0->m_A.x > s1->m_A.x)
		return 1;
	return 0;
}

void CBotEngine::GenerateCorners()
{
	int CornerCount = 0;
	for(int i = 1; i < m_Width - 1; i++)
	{
		for(int j = 1; j < m_Height - 1; j++)
		{
			if((m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR)
				continue;
			int n = 0;
			for(int k = 0; k < 8; k++)
			{
				int Index = i + g_Neighboors[k][0] + (j + g_Neighboors[k][1]) * m_Width;
				if((m_pGrid[Index] & GTILE_MASK) > GTILE_AIR)
					n += g_PowerTwo[k];
			}
			if(g_IsOuterCorner[n])
				CornerCount++;
		}
	}
	dbg_msg("botengine", "Found %d outer corners", CornerCount);
	m_pCorners = (vec2 *)std::malloc(CornerCount * sizeof(vec2));
	m_CornerCount = CornerCount;
	int m = 0;
	for(int i = 1; i < m_Width - 1; i++)
	{
		for(int j = 1; j < m_Height - 1; j++)
		{
			if((m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR)
				continue;
			int n = 0;
			for(int k = 0; k < 8; k++)
			{
				int Index = i + g_Neighboors[k][0] + (j + g_Neighboors[k][1]) * m_Width;
				if((m_pGrid[Index] & GTILE_MASK) > GTILE_AIR)
					n += g_PowerTwo[k];
			}
			if(g_IsOuterCorner[n])
				m_pCorners[m++] = ConvertIndex(i + j * m_Width);
		}
	}
}

void CBotEngine::GenerateTriangles()
{
	int CornerCount = 0;
	for(int i = 1; i < m_Width - 1; i++)
	{
		for(int j = 1; j < m_Height - 1; j++)
		{
			if((m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR)
				continue;
			int n = 0;
			for(int k = 0; k < 8; k++)
			{
				int Index = i + g_Neighboors[k][0] + (j + g_Neighboors[k][1]) * m_Width;
				if((m_pGrid[Index] & GTILE_MASK) > GTILE_AIR)
					n += g_PowerTwo[k];
			}
			if(g_IsInnerCorner[n] || g_IsOuterCorner[n])
				CornerCount++;
		}
	}
	dbg_msg("botengine", "Found %d corners", CornerCount);
	const int CandidateCount = CornerCount;
	vec2 *Corners = (vec2 *)std::malloc((CandidateCount + 3) * sizeof(vec2));
	if(!Corners || CandidateCount < 3)
	{
		std::free(Corners);
		m_Triangulation.m_Size = 0;
		return;
	}
	int m = 0;
	for(int i = 1; i < m_Width - 1; i++)
	{
		for(int j = 1; j < m_Height - 1; j++)
		{
			if((m_pGrid[i + j * m_Width] & GTILE_MASK) > GTILE_AIR)
				continue;
			int n = 0;
			for(int k = 0; k < 8; k++)
			{
				int Index = i + g_Neighboors[k][0] + (j + g_Neighboors[k][1]) * m_Width;
				if((m_pGrid[Index] & GTILE_MASK) > GTILE_AIR)
					n += g_PowerTwo[k];
			}
			if(g_IsInnerCorner[n] || g_IsOuterCorner[n])
				Corners[m++] = vec2(i, j);
		}
	}
	// The triangulation below checks corner triples and scans other corners for
	// every candidate triangle. Keep a spatially spread sample so larger maps do
	// not block the game thread for minutes the first time a pet is spawned.
	constexpr int MAX_NAVIGATION_CORNERS = 64;
	if(CornerCount > MAX_NAVIGATION_CORNERS)
	{
		std::vector<vec2> SampledCorners;
		std::vector<float> NearestDistance(CornerCount, 1e30f);
		std::vector<bool> Selected(CornerCount, false);
		SampledCorners.reserve(MAX_NAVIGATION_CORNERS);
		int SelectedIndex = 0;
		while(SelectedIndex >= 0 && static_cast<int>(SampledCorners.size()) < MAX_NAVIGATION_CORNERS)
		{
			Selected[SelectedIndex] = true;
			const vec2 SelectedCorner = Corners[SelectedIndex];
			SampledCorners.push_back(SelectedCorner);
			for(int Candidate = 0; Candidate < CornerCount; Candidate++)
			{
				const float dx = Corners[Candidate].x - SelectedCorner.x;
				const float dy = Corners[Candidate].y - SelectedCorner.y;
				NearestDistance[Candidate] = std::min(NearestDistance[Candidate], dx * dx + dy * dy);
			}
			SelectedIndex = -1;
			float FarthestDistance = -1.0f;
			for(int Candidate = 0; Candidate < CornerCount; Candidate++)
			{
				if(!Selected[Candidate] && NearestDistance[Candidate] > FarthestDistance)
				{
					FarthestDistance = NearestDistance[Candidate];
					SelectedIndex = Candidate;
				}
			}
		}
		std::copy(SampledCorners.begin(), SampledCorners.end(), Corners);
		CornerCount = static_cast<int>(SampledCorners.size());
		dbg_msg("botengine", "Reduced navigation corners from %d to %d", CandidateCount, CornerCount);
	}
	m_Triangulation.m_pTriangles = (CTriangulation::CTriangleData *)std::malloc(2 * CornerCount * sizeof(CTriangulation::CTriangleData));
	if(!m_Triangulation.m_pTriangles)
	{
		std::free(Corners);
		m_Triangulation.m_Size = 0;
		return;
	}
	vec2 BL = Corners[0], TR = Corners[0];
	for(int i = 1; i < CornerCount; i++)
	{
		vec2 c = Corners[i];
		if(c.x < BL.x)
			BL.x = c.x;
		if(c.y < BL.y)
			BL.y = c.y;
		if(c.x > TR.x)
			TR.x = c.x;
		if(c.y > TR.y)
			TR.y = c.y;
	}
	Corners[CornerCount] = BL;
	Corners[CornerCount + 1].x = 2 * TR.x - BL.x;
	Corners[CornerCount + 1].y = BL.y;
	Corners[CornerCount + 2].x = BL.x;
	Corners[CornerCount + 2].y = 2 * TR.y - BL.y;

	m_Triangulation.m_Size = 0;
	// CTriangle triangle(Corners[CornerCount], Corners[CornerCount+1], Corners[CornerCount+2]);
	// m_Triangulation.m_pTriangles[m_Triangulation.m_Size++].m_Triangle = triangle;
	for(int i = 0; i < CornerCount - 2; i++)
	{
		for(int j = i + 1; j < CornerCount - 1; j++)
		{
			if(FastIntersectLine(Corners[i].x + Corners[i].y * m_Width, Corners[j].x + Corners[j].y * m_Width))
				continue;
			for(int k = j + 1; k < CornerCount; k++)
			{
				if(FastIntersectLine(Corners[i].x + Corners[i].y * m_Width, Corners[k].x + Corners[k].y * m_Width) || FastIntersectLine(Corners[j].x + Corners[j].y * m_Width, Corners[k].x + Corners[k].y * m_Width))
					continue;
				CTriangle triangle(Corners[i], Corners[j], Corners[k]);
				if(triangle.IsFlat())
					continue;
				vec2 cc = triangle.OuterCircleCenter();
				float radius = distance(Corners[i], cc);

				bool found = false;
				for(int w = 0; w < m_Triangulation.m_Size; w++)
				{
					if(triangle.Intersects(m_Triangulation.m_pTriangles[w].m_Triangle))
					{
						found = true;
						break;
					}
				}
				if(found)
					continue;
				for(int w = 0; w < CornerCount; w++)
				{
					if(w == i || w == j || w == k)
						continue;
					if(distance(cc, Corners[w]) < radius)
					{
						found = true;
						break;
					}
				}
				if(found)
					continue;

				m_Triangulation.m_pTriangles[m_Triangulation.m_Size].m_Triangle = triangle;

				m_Triangulation.m_Size++;
			}
		}
	}
	for(int i = 0; i < CornerCount - 2; i++)
	{
		for(int j = i + 1; j < CornerCount - 1; j++)
		{
			if(FastIntersectLine(Corners[i].x + Corners[i].y * m_Width, Corners[j].x + Corners[j].y * m_Width))
				continue;
			for(int k = j + 1; k < CornerCount; k++)
			{
				if(FastIntersectLine(Corners[i].x + Corners[i].y * m_Width, Corners[k].x + Corners[k].y * m_Width) || FastIntersectLine(Corners[j].x + Corners[j].y * m_Width, Corners[k].x + Corners[k].y * m_Width))
					continue;
				CTriangle triangle(Corners[i], Corners[j], Corners[k]);
				if(triangle.IsFlat())
					continue;

				bool found = false;
				for(int w = 0; w < m_Triangulation.m_Size; w++)
				{
					if(triangle.Intersects(m_Triangulation.m_pTriangles[w].m_Triangle) || (triangle.m_aPoints[0] == m_Triangulation.m_pTriangles[w].m_Triangle.m_aPoints[0] && triangle.m_aPoints[1] == m_Triangulation.m_pTriangles[w].m_Triangle.m_aPoints[1] && triangle.m_aPoints[2] == m_Triangulation.m_pTriangles[w].m_Triangle.m_aPoints[2]))
					{
						found = true;
						break;
					}
				}
				if(found)
					continue;
				for(int w = 0; w < CornerCount; w++)
				{
					if(w == i || w == j || w == k)
						continue;
					if(triangle.Inside(Corners[w]))
					{
						found = true;
						break;
					}
				}
				if(found)
					continue;

				m_Triangulation.m_pTriangles[m_Triangulation.m_Size].m_Triangle = triangle;

				m_Triangulation.m_Size++;
			}
		}
	}
	dbg_msg("botengine", "Build %d triangles", m_Triangulation.m_Size);
	std::free(Corners);
}

void CBotEngine::GenerateGraphFromTriangles()
{
	m_Graph.m_NumEdges = 0;
	for(int i = 0; i < m_Triangulation.m_Size - 1; i++)
	{
		for(int j = i + 1; j < m_Triangulation.m_Size; j++)
		{
			int count = 0;
			for(int k = 0; k < 3; k++)
				for(int l = 0; l < 3; l++)
					if(m_Triangulation.m_pTriangles[i].m_Triangle.m_aPoints[k] == m_Triangulation.m_pTriangles[j].m_Triangle.m_aPoints[l])
						count++;
			if(count == 2)
				m_Graph.m_NumEdges += 2;
		}
	}
	m_Graph.m_Width = m_Width;
	m_Graph.m_NumVertices = m_Triangulation.m_Size;
	m_Graph.m_pVertices = (CVertex *)std::malloc(m_Graph.m_NumVertices * sizeof(CVertex));
	m_Graph.m_pEdges = (CEdge *)std::malloc(m_Graph.m_NumEdges * sizeof(CEdge));
	for(int i = 0; i < m_Triangulation.m_Size; i++)
		m_Graph.m_pVertices[i].m_Pos = m_Triangulation.m_pTriangles[i].m_Triangle.Centroid() * 32 + vec2(16, 16);
	CEdge *pEdge = m_Graph.m_pEdges;
	for(int i = 0; i < m_Triangulation.m_Size - 1; i++)
	{
		for(int j = i + 1; j < m_Triangulation.m_Size; j++)
		{
			int count = 0;
			for(int k = 0; k < 3; k++)
				for(int l = 0; l < 3; l++)
					if(m_Triangulation.m_pTriangles[i].m_Triangle.m_aPoints[k] == m_Triangulation.m_pTriangles[j].m_Triangle.m_aPoints[l])
						count++;
			if(count == 2)
			{
				pEdge->m_Start = m_Graph.m_pVertices[i].m_Pos;
				pEdge->m_StartID = i;
				pEdge->m_End = m_Graph.m_pVertices[j].m_Pos;
				pEdge->m_EndID = j;
				pEdge->m_Size = 2;
				pEdge++;
				pEdge->m_Start = m_Graph.m_pVertices[j].m_Pos;
				pEdge->m_StartID = j;
				pEdge->m_End = m_Graph.m_pVertices[i].m_Pos;
				pEdge->m_EndID = i;
				pEdge->m_Size = 2;
				pEdge++;
			}
		}
	}
	dbg_msg("botengine", "Create graph, %d vertices, %d edges", m_Graph.m_NumVertices, m_Graph.m_NumEdges);
	m_Graph.ComputeClosestPath();
}

int CBotEngine::GetTile(int x, int y)
{
	x = std::clamp(x, 0, m_Width - 1);
	y = std::clamp(y, 0, m_Height - 1);
	return m_pGrid[y * m_Width + x];
}

int CBotEngine::GetTile(vec2 Pos)
{
	return GetTile(round_to_int(Pos.x / 32), round_to_int(Pos.y / 32));
}

int CBotEngine::ConvertFromIndex(vec2 Pos)
{
	return std::clamp(round_to_int(Pos.x / 32), 0, m_Width - 1) + std::clamp(round_to_int(Pos.y / 32), 0, m_Width - 1) * m_Width;
}

int CBotEngine::FastIntersectLine(int Id1, int Id2)
{
	int i1 = Id1 % m_Width;
	int i2 = Id2 % m_Width;
	int j1 = Id1 / m_Width;
	int j2 = Id2 / m_Width;
	int di = abs(i2 - i1);
	int dj = abs(j2 - j1);
	int i = 0;
	int j = 0;
	int si = (i2 > i1) ? 1 : (i2 < i1) ? -1 : 0;
	int sj = (j2 > j1) ? 1 : (j2 < j1) ? -1 : 0;
	int Neigh[4] = {Id1 + si, Id2 - si, Id1 + sj * m_Width, Id2 - sj * m_Width};
	for(int k = 0; k < 4; k++)
		if((GetTile(Neigh[k]) & GTILE_MASK) > GTILE_AIR)
			return GetTile(Neigh[k]);
	while(i <= di && j <= dj)
	{
		if((GetTile(i * si + i1, j * sj + j1) & GTILE_MASK) > GTILE_AIR)
			return GetTile(i * si + i1, j * sj + j1);
		if(abs((j + 1) * di - i * dj) < abs(j * di - (i + 1) * dj))
			j++;
		else
			i++;
	}
	return 0;
}

void CBotEngine::GetPath(vec2 VStart, vec2 VEnd, CPath *pPath)
{
	if(!HasRoute(VStart, VEnd))
	{
		pPath->m_Size = 0;
		return;
	}
	pPath->m_Size = m_Graph.GetPath(GetClosestVertex(VStart), GetClosestVertex(VEnd), pPath->m_pVertices + 1);
	pPath->m_pVertices[0] = VStart;
	pPath->m_pVertices[pPath->m_Size + 1] = VEnd;
	pPath->m_Size = pPath->m_Size + 2;
}

bool CBotEngine::HasRoute(vec2 VStart, vec2 VEnd)
{
	if(m_Graph.m_NumVertices <= 0 || !m_Graph.m_pVertices || !m_Graph.m_pClosestPath)
		return false;
	const int Start = GetClosestVertex(VStart);
	const int End = GetClosestVertex(VEnd);
	return Start == End || m_Graph.m_pClosestPath[Start + End * m_Graph.m_NumVertices] >= 0;
}

int CBotEngine::GetPartialPath(vec2 Pos, vec2 Target, vec2 *pVertices, int MaxSize)
{
	int id[2] = {0};
	vec2 aPos[] = {Pos, Target};
	for(int j = 0; j < 2; j++)
	{
		int d = 1000;
		vec2 pt = aPos[j] / 32;
		for(int k = 0; k < m_Triangulation.m_Size; k++)
		{
			if(m_Triangulation.m_pTriangles[k].m_Triangle.Inside(pt))
			{
				id[j] = k;
				break;
			}
			int dist = distance(m_Graph.m_pVertices[k].m_Pos, aPos[j]);
			if(dist < d)
			{
				d = dist;
				id[j] = k;
			}
		}
	}
	int n = m_Graph.m_pClosestPath[id[0] + id[1] * m_Graph.m_NumVertices];
	if(n < 0)
		return 0;
	pVertices[0] = Pos;
	int Size = 1;
	while(Size < MaxSize - 1 && id[0] != id[1])
	{
		pVertices[Size++] = m_Graph.m_pVertices[id[0]].m_Pos;
		id[0] = m_Graph.m_pClosestPath[id[0] + id[1] * m_Graph.m_NumVertices];
	}
	pVertices[Size++] = m_Graph.m_pVertices[id[0]].m_Pos;
	if(Size < MaxSize)
		pVertices[Size++] = Target;
	return Size;
}

vec2 CBotEngine::NextPoint(vec2 Pos, vec2 Target)
{
	int id[2] = {0};
	vec2 aPos[] = {Pos, Target};
	for(int j = 0; j < 2; j++)
	{
		int d = 1000;
		vec2 pt = aPos[j] / 32;
		for(int k = 0; k < m_Triangulation.m_Size; k++)
		{
			if(m_Triangulation.m_pTriangles[k].m_Triangle.Inside(pt))
			{
				id[j] = k;
				break;
			}
			int dist = distance(m_Graph.m_pVertices[k].m_Pos, aPos[j]);
			if(dist < d)
			{
				d = dist;
				id[j] = k;
			}
		}
	}
	int n = m_Graph.m_pClosestPath[id[0] + id[1] * m_Graph.m_NumVertices];
	if(n < 0)
		return Target;
	return m_Graph.m_pVertices[n].m_Pos;
}

int CBotEngine::DistanceToEdge(CEdge Edge, vec2 Pos)
{
	if(!Edge.m_Size)
		return -1;

	vec2 InterPos;
	closest_point_on_line(Edge.m_Start, Edge.m_End, Pos, InterPos);
	return distance(InterPos, Pos);
}

// Need something smarter
int CBotEngine::FarestPointOnEdge(CPath *pPath, vec2 Pos, vec2 *pTarget)
{
	for(int k = pPath->m_Size - 1; k >= 0; k--)
	{
		int D = distance(Pos, pPath->m_pVertices[k]);
		if(D < 1000)
		{
			vec2 VertexPos = pPath->m_pVertices[k];
			vec2 W = direction(angle(normalize(VertexPos - Pos)) + pi / 2) * 14.f;
			if(!(GameServer()->Collision()->FastIntersectLine(Pos - W, VertexPos - W, 0, 0)) && !(GameServer()->Collision()->FastIntersectLine(Pos + W, VertexPos + W, 0, 0)))
			{
				if(pTarget)
					*pTarget = VertexPos;
				return D;
			}
		}
	}
	return -1;
}

int CBotEngine::GetClosestEdge(vec2 Pos, int ClosestRange, CEdge *pEdge)
{
	CEdge *ClosestEdge = 0;

	for(int k = 0; k < m_Graph.m_NumEdges; k++)
	{
		CEdge *pE = m_Graph.m_pEdges + k;
		int D = DistanceToEdge(*pE, Pos);
		if(D < 0)
			continue;
		if(D < ClosestRange || ClosestRange < 0)
		{
			ClosestEdge = pE;
			ClosestRange = D;
		}
	}
	if(ClosestEdge)
	{
		if(pEdge)
		{
			*pEdge = *ClosestEdge;
		}
		return ClosestRange;
	}
	return -1;
}

int CBotEngine::GetClosestVertex(vec2 Pos)
{
	int i = 0;
	int d = 1000;
	vec2 pt = Pos / 32;
	for(int k = 0; k < m_Triangulation.m_Size; k++)
	{
		if(m_Triangulation.m_pTriangles[k].m_Triangle.Inside(pt))
			return k;
		int dist = distance(m_Graph.m_pVertices[k].m_Pos, Pos);
		if(dist < d)
		{
			d = dist;
			i = k;
		}
	}
	return i;
}

void CBotEngine::OnCharacterDeath(int Victim, int Killer, int Weapon)
{
	if(m_apBot[Victim])
	{
		RecordNavigationDeath(Victim);
		m_apBot[Victim]->m_GenomeTick >>= 1;
	}
	if(m_apBot[Killer])
		m_apBot[Killer]->m_GenomeTick <<= 1;
}

void CBotEngine::TrackBotPosition(int ClientId, vec2 Pos, int Tick)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || m_Width <= 0 || m_Height <= 0)
		return;
	const int X = std::clamp(static_cast<int>(Pos.x / 32.0f), 0, m_Width - 1);
	const int Y = std::clamp(static_cast<int>(Pos.y / 32.0f), 0, m_Height - 1);
	const int Tile = Y * m_Width + X;
	SBotTrail &Trail = m_aBotTrails[ClientId];
	if(Tile == Trail.m_LastTile)
		return;
	const int ElapsedTicks = Trail.m_LastTileTick >= 0 ? std::max(1, Tick - Trail.m_LastTileTick) : -1;
	Trail.m_LastTile = Tile;
	Trail.m_LastTileTick = Tick;
	if(Trail.m_Count < static_cast<int>(std::size(Trail.m_aTiles)))
		Trail.m_aTiles[Trail.m_Count++] = Tile;
	else
	{
		std::move(Trail.m_aTiles + 1, Trail.m_aTiles + std::size(Trail.m_aTiles), Trail.m_aTiles);
		Trail.m_aTiles[std::size(Trail.m_aTiles) - 1] = Tile;
	}
	if(Tile >= 0 && Tile < static_cast<int>(m_vNavigationDeaths.size()) && GameServer()->db &&
		GameServer()->Map() && IsSupportedBotLearningMap(GameServer()->Map()->BaseName()))
	{
		sqlite3_stmt *pStmt = nullptr;
		if(sqlite3_prepare_v2(GameServer()->db, "INSERT INTO TT_BOT_NAV_LEARNING (MAP_NAME, TILE_INDEX, VISITS) VALUES (?1, ?2, 1) ON CONFLICT(MAP_NAME, TILE_INDEX) DO UPDATE SET VISITS = VISITS + 1", -1, &pStmt, nullptr) == SQLITE_OK)
		{
			sqlite3_bind_text(pStmt, 1, GameServer()->Map()->BaseName(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(pStmt, 2, Tile);
			sqlite3_step(pStmt);
		}
		sqlite3_finalize(pStmt);
		if(ElapsedTicks > 0)
		{
			m_vNavigationBestTicks[Tile] = std::min(m_vNavigationBestTicks[Tile], ElapsedTicks);
			if(sqlite3_prepare_v2(GameServer()->db, "INSERT INTO TT_BOT_NAV_SPEED (MAP_NAME, TILE_INDEX, BEST_TICKS) VALUES (?1, ?2, ?3) ON CONFLICT(MAP_NAME, TILE_INDEX) DO UPDATE SET BEST_TICKS = MIN(BEST_TICKS, excluded.BEST_TICKS)", -1, &pStmt, nullptr) == SQLITE_OK)
			{
				sqlite3_bind_text(pStmt, 1, GameServer()->Map()->BaseName(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_int(pStmt, 2, Tile);
				sqlite3_bind_int(pStmt, 3, ElapsedTicks);
				sqlite3_step(pStmt);
			}
			sqlite3_finalize(pStmt);
		}
	}
}

int CBotEngine::NavigationDeathPenalty(vec2 Pos) const
{
	if(m_Width <= 0 || m_Height <= 0 || m_vNavigationDeaths.empty())
		return 0;
	const int X = std::clamp(static_cast<int>(Pos.x / 32.0f), 0, m_Width - 1);
	const int Y = std::clamp(static_cast<int>(Pos.y / 32.0f), 0, m_Height - 1);
	return m_vNavigationDeaths[Y * m_Width + X];
}

int CBotEngine::NavigationBestTileTicks(vec2 Pos) const
{
	if(m_Width <= 0 || m_Height <= 0 || m_vNavigationBestTicks.empty())
		return std::numeric_limits<int>::max();
	const int X = std::clamp(static_cast<int>(Pos.x / 32.0f), 0, m_Width - 1);
	const int Y = std::clamp(static_cast<int>(Pos.y / 32.0f), 0, m_Height - 1);
	return m_vNavigationBestTicks[Y * m_Width + X];
}

void CBotEngine::RecordNavigationDeath(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameServer()->db || !GameServer()->Map() ||
		!IsSupportedBotLearningMap(GameServer()->Map()->BaseName()))
		return;
	SBotTrail &Trail = m_aBotTrails[ClientId];
	sqlite3_stmt *pStmt = nullptr;
	if(sqlite3_prepare_v2(GameServer()->db, "INSERT INTO TT_BOT_NAV_LEARNING (MAP_NAME, TILE_INDEX, DEATHS) VALUES (?1, ?2, 1) ON CONFLICT(MAP_NAME, TILE_INDEX) DO UPDATE SET DEATHS = DEATHS + 1", -1, &pStmt, nullptr) != SQLITE_OK)
		return;
	const int Begin = std::max(0, Trail.m_Count - 6);
	for(int i = Begin; i < Trail.m_Count; i++)
	{
		const int Tile = Trail.m_aTiles[i];
		if(Tile < 0 || Tile >= static_cast<int>(m_vNavigationDeaths.size()))
			continue;
		m_vNavigationDeaths[Tile] = static_cast<uint8_t>(std::min(255, m_vNavigationDeaths[Tile] + 1));
		sqlite3_reset(pStmt);
		sqlite3_clear_bindings(pStmt);
		sqlite3_bind_text(pStmt, 1, GameServer()->Map()->BaseName(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(pStmt, 2, Tile);
		sqlite3_step(pStmt);
	}
	sqlite3_finalize(pStmt);
	Trail.m_Count = 0;
	Trail.m_LastTile = -1;
	Trail.m_LastTileTick = -1;
}

void CBotEngine::RegisterBot(int CID, CBot *pBot)
{
	if(CID >= 0 && CID < MAX_CLIENTS)
	{
		m_apBot[CID] = pBot;
		mem_zero(&m_aBotTrails[CID], sizeof(m_aBotTrails[CID]));
		m_aBotTrails[CID].m_LastTile = -1;
		m_aBotTrails[CID].m_LastTileTick = -1;
	}
}

void CBotEngine::UnRegisterBot(int CID)
{
	if(CID >= 0 && CID < MAX_CLIENTS)
	{
		m_apBot[CID] = 0;
		mem_zero(&m_aBotTrails[CID], sizeof(m_aBotTrails[CID]));
		m_aBotTrails[CID].m_LastTile = -1;
		m_aBotTrails[CID].m_LastTileTick = -1;
	}
}

int CBotEngine::NetworkClipped(int SnappingClient, vec2 CheckPos)
{
	if(SnappingClient == -1)
		return 1;

	float dx = GameServer()->GetPlayerChar(SnappingClient)->GetPos().x - CheckPos.x;
	float dy = GameServer()->GetPlayerChar(SnappingClient)->GetPos().y - CheckPos.y;

	if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
		return 1;

	if(distance(GameServer()->GetPlayerChar(SnappingClient)->GetPos(), CheckPos) > 1100.0f)
		return 1;
	return 0;
}

void CBotEngine::Snap(int SnappingClient)
{
	for(int k = 0; k < m_SegmentCount; k++)
	{
		CSegment *pSegment = m_pSegments + k;
		if(pSegment->m_SnapID < 0)
			continue;
		vec2 From = pSegment->m_A;
		vec2 To = pSegment->m_B;
		if(NetworkClipped(SnappingClient, To) && NetworkClipped(SnappingClient, From))
			continue;
		CNetObj_Laser Laser{};
		Laser.m_X = (int)To.x;
		Laser.m_Y = (int)To.y;
		Laser.m_FromX = (int)From.x;
		Laser.m_FromY = (int)From.y;
		Laser.m_StartTick = GameServer()->Server()->Tick();
		GameServer()->Server()->SnapNewItem(NETOBJTYPE_LASER, pSegment->m_SnapID, &Laser, sizeof(Laser));
	}
}
