#!/usr/bin/env python3
"""
C 서버 테스트 클라이언트
LocalKnowledgeBase.c 서버를 테스트
"""

import requests
import json
import sys
import time

# 서버 설정
SERVER_URL = "http://localhost:7777"
SEARCH_ENDPOINT = f"{SERVER_URL}/search"

def test_basic_search():
    """기본 검색 테스트"""
    print("=" * 60)
    print("테스트 1: 기본 검색 (단순 query)")
    print("-" * 60)

    payload = {
        "query": "MSX computer",
        "count": 5
    }

    try:
        response = requests.post(SEARCH_ENDPOINT, json=payload, timeout=5)
        print(f"Status Code: {response.status_code}")

        if response.status_code == 200:
            data = response.json()
            print(f"✓ 응답 성공")
            print(f"  - results 개수: {len(data.get('results', []))}")
            print(f"  - took_ms: {data.get('took_ms')}")
            print(f"  - total: {data.get('total')}")
            print(f"  - engine: {data.get('engine')}")

            if data.get('results'):
                print(f"\n첫 번째 결과:")
                result = data['results'][0]
                print(f"  - link: {result.get('link')}")
                print(f"  - title: {result.get('title')}")
                print(f"  - snippet: {result.get('snippet', '')[:100]}...")
            return True
        else:
            print(f"✗ 실패: {response.text}")
            return False

    except Exception as e:
        print(f"✗ 에러: {e}")
        return False

def test_queries_array():
    """queries 배열 테스트"""
    print("\n" + "=" * 60)
    print("테스트 2: queries 배열")
    print("-" * 60)

    payload = {
        "queries": ["x68000", "Sharp computer"],
        "count": 3
    }

    try:
        response = requests.post(SEARCH_ENDPOINT, json=payload, timeout=5)
        print(f"Status Code: {response.status_code}")

        if response.status_code == 200:
            data = response.json()
            print(f"✓ 응답 성공")
            print(f"  - results 개수: {len(data.get('results', []))}")
            return True
        else:
            print(f"✗ 실패: {response.text}")
            return False

    except Exception as e:
        print(f"✗ 에러: {e}")
        return False

def test_json_query():
    """JSON 형식 쿼리 테스트"""
    print("\n" + "=" * 60)
    print("테스트 3: JSON 형식 query")
    print("-" * 60)

    payload = {
        "query": '{"queries": ["retro computer", "vintage"]}',
        "count": 5
    }

    try:
        response = requests.post(SEARCH_ENDPOINT, json=payload, timeout=5)
        print(f"Status Code: {response.status_code}")

        if response.status_code == 200:
            data = response.json()
            print(f"✓ 응답 성공")
            print(f"  - results 개수: {len(data.get('results', []))}")
            return True
        else:
            print(f"✗ 실패: {response.text}")
            return False

    except Exception as e:
        print(f"✗ 에러: {e}")
        return False

def test_empty_query():
    """빈 쿼리 테스트"""
    print("\n" + "=" * 60)
    print("테스트 4: 빈 쿼리 처리")
    print("-" * 60)

    payload = {
        "query": "",
        "count": 5
    }

    try:
        response = requests.post(SEARCH_ENDPOINT, json=payload, timeout=5)
        print(f"Status Code: {response.status_code}")

        if response.status_code == 200:
            data = response.json()
            print(f"✓ 응답 성공 (빈 결과 예상)")
            print(f"  - results 개수: {len(data.get('results', []))}")
            print(f"  - total: {data.get('total')}")

            if data.get('total') == 0:
                print(f"✓ 빈 쿼리 정상 처리")
                return True
            else:
                print(f"✗ 예상과 다름")
                return False
        else:
            print(f"✗ 실패: {response.text}")
            return False

    except Exception as e:
        print(f"✗ 에러: {e}")
        return False

def test_response_format():
    """응답 형식 검증"""
    print("\n" + "=" * 60)
    print("테스트 5: 응답 형식 검증")
    print("-" * 60)

    payload = {
        "query": "test",
        "count": 1
    }

    try:
        response = requests.post(SEARCH_ENDPOINT, json=payload, timeout=5)

        if response.status_code == 200:
            data = response.json()

            # 필수 필드 검증
            required_fields = ['results', 'took_ms', 'total', 'engine']
            missing_fields = [f for f in required_fields if f not in data]

            if missing_fields:
                print(f"✗ 누락된 필드: {missing_fields}")
                return False

            print(f"✓ 모든 필수 필드 존재")

            # results 형식 검증
            if data.get('results'):
                result = data['results'][0]
                result_fields = ['link', 'title', 'snippet']
                missing_result_fields = [f for f in result_fields if f not in result]

                if missing_result_fields:
                    print(f"✗ 결과에 누락된 필드: {missing_result_fields}")
                    return False

                print(f"✓ 결과 형식 정상")

            return True
        else:
            print(f"✗ 실패: {response.text}")
            return False

    except Exception as e:
        print(f"✗ 에러: {e}")
        return False

def test_server_health():
    """서버 상태 확인"""
    print("=" * 60)
    print("서버 연결 테스트")
    print("-" * 60)

    try:
        response = requests.get(SERVER_URL, timeout=2)
        print(f"✓ 서버 응답: {response.status_code}")
        return True
    except requests.exceptions.ConnectionError:
        print(f"✗ 서버에 연결할 수 없습니다: {SERVER_URL}")
        print(f"  서버가 실행 중인지 확인하세요.")
        return False
    except Exception as e:
        print(f"✗ 에러: {e}")
        return False

def main():
    print("LocalKnowledgeBase C 서버 테스트")
    print("=" * 60)
    print(f"대상 서버: {SERVER_URL}")
    print()

    # 서버 연결 확인
    if not test_server_health():
        print("\n서버를 시작하고 다시 시도하세요:")
        print("  ./LocalKnowledgeBase")
        sys.exit(1)

    print()

    # 테스트 실행
    tests = [
        ("기본 검색", test_basic_search),
        ("queries 배열", test_queries_array),
        ("JSON 쿼리", test_json_query),
        ("빈 쿼리", test_empty_query),
        ("응답 형식", test_response_format)
    ]

    results = []
    for name, test_func in tests:
        result = test_func()
        results.append((name, result))
        time.sleep(0.1)  # 테스트 간 간격

    # 결과 요약
    print("\n" + "=" * 60)
    print("테스트 결과 요약")
    print("=" * 60)

    passed = sum(1 for _, result in results if result)
    total = len(results)

    for name, result in results:
        status = "✓ 통과" if result else "✗ 실패"
        print(f"{status}: {name}")

    print()
    print(f"총 {total}개 테스트 중 {passed}개 통과")

    if passed == total:
        print("\n✅ 모든 테스트 통과!")
        sys.exit(0)
    else:
        print(f"\n❌ {total - passed}개 테스트 실패")
        sys.exit(1)

if __name__ == "__main__":
    main()
