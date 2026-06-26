from blueprint_agent_tools.output_layout import make_safe_path


def test_make_safe_path_basic():
    assert make_safe_path("/Game/Blueprints/BP_Hero") == "Game__Blueprints__BP_Hero"


def test_make_safe_path_no_leading_slash():
    assert make_safe_path("Game/Foo") == "Game__Foo"


def test_make_safe_path_root_only():
    assert make_safe_path("/Game") == "Game"
