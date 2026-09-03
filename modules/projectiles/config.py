def can_build(env, platform):
    # Every platform. A module that quietly drops out on one platform is a module
    # the export templates ship without, and that fails at runtime rather than at
    # export time.
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "ProjectilePool",
        "ProjectileKind",
    ]


def get_doc_path():
    return "doc_classes"
