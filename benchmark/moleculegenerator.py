from abc import abstractmethod, ABC


class MoleculeGenerator(ABC):

    @abstractmethod
    def generate(self, molecule):
        pass