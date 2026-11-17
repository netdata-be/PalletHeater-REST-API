# EasyButton 2.0.3 compiles EasyButtonTouch.cpp, which includes <Filter.h> from a
# library it does not declare as a dependency, so a clean build fails. The touch
# button class is not used here: leave that one source file out of the build.
Import("env")


def _skip_touch(env, node):
    return None


env.AddBuildMiddleware(_skip_touch, "*EasyButtonTouch.cpp")
