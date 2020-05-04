# -- Project information -----------------------------------------------------

project = 'Tralics'
author = 'INRIA and CNRS'
copyright = '2020, INRIA and CNRS'
release = '2.16.0pre'

# -- General configuration ---------------------------------------------------

extensions = ['sphinx_rtd_theme', 'breathe']
templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

# -- Breathe configuration

breathe_projects = {"tralics": "../xml/"}
breathe_default_project = "tralics"
breathe_default_members = ('members', 'undoc-members')

# -- Options for HTML output -------------------------------------------------

# html_static_path = ['_static']
html_show_sourcelink = False
html_theme = 'sphinx_rtd_theme'
html_theme_options = {
    'prev_next_buttons_location': None, }
