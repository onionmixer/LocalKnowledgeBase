#!/bin/bash

echo "=== LocalKnowledgeBase 통합 테스트 ==="
echo ""

# 색상 정의
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 서버 확인
echo "1. 서버 상태 확인..."
if curl -s -o /dev/null -w "%{http_code}" http://localhost:7777 > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} 서버 실행 중"
else
    echo -e "${RED}✗${NC} 서버가 실행되지 않음"
    echo ""
    echo "서버를 시작하세요:"
    echo "  Python: python3 LimitedKnowledgeBase.py"
    echo "  C:      ./LocalKnowledgeBase"
    exit 1
fi

echo ""
echo "2. 기본 검색 테스트..."
RESPONSE=$(curl -s -X POST http://localhost:7777/search \
    -H "Content-Type: application/json" \
    -d '{"query": "test", "count": 3}')

if echo "$RESPONSE" | jq -e '.results' > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} 응답 형식 정상"
    echo "   결과 수: $(echo "$RESPONSE" | jq -r '.total')"
    echo "   엔진: $(echo "$RESPONSE" | jq -r '.engine')"
else
    echo -e "${RED}✗${NC} 응답 형식 오류"
    echo "$RESPONSE"
    exit 1
fi

echo ""
echo "3. queries 배열 테스트..."
RESPONSE=$(curl -s -X POST http://localhost:7777/search \
    -H "Content-Type: application/json" \
    -d '{"queries": ["MSX", "computer"], "count": 5}')

if echo "$RESPONSE" | jq -e '.results' > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC} queries 배열 처리 정상"
else
    echo -e "${RED}✗${NC} queries 배열 처리 실패"
    exit 1
fi

echo ""
echo "4. 빈 쿼리 테스트..."
RESPONSE=$(curl -s -X POST http://localhost:7777/search \
    -H "Content-Type: application/json" \
    -d '{"query": "", "count": 5}')

TOTAL=$(echo "$RESPONSE" | jq -r '.total')
if [ "$TOTAL" = "0" ]; then
    echo -e "${GREEN}✓${NC} 빈 쿼리 정상 처리 (결과: 0)"
else
    echo -e "${YELLOW}⚠${NC} 빈 쿼리 처리 확인 필요 (결과: $TOTAL)"
fi

echo ""
echo "5. Python 테스트 스크립트 실행..."
if python3 test_c_server.py; then
    echo -e "${GREEN}✓${NC} Python 테스트 통과"
else
    echo -e "${RED}✗${NC} Python 테스트 실패"
    exit 1
fi

echo ""
echo "=== 통합 테스트 완료 ==="
echo -e "${GREEN}✅ 모든 테스트 통과!${NC}"
