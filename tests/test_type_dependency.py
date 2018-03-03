import unittest
from compile_dsdl import *

class TypeDependencyTestCase(unittest.TestCase):
    def test_dependencies_are_resolved_correctly(self):
        dependencies = {
            'master': ['slave1', 'slave2'],
            'slave1': ['slave2'],
            'slave2': [],
        }

        solution = list(topological_sort(dependencies))
        self.assertEqual(solution, ['slave2', 'slave1', 'master'])

    def test_cyclic_dependency(self):
        dependencies = {
            'master': ['slave1', 'slave2'],
            'slave1': ['slave2'],
            'slave2': ['master'],
        }

        with self.assertRaises(ValueError):
            list(topological_sort(dependencies))

