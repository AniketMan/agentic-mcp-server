"""Entry point for: python -m ue56-level-editor"""
import sys
sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent))
from core.cli import main
sys.exit(main())
