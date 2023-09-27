import logging
import shutil
from pathlib import Path

import click
import jinja2

LOGGER = logging.getLogger(__name__)

root = Path(__file__).parent
data_dir = root

ANALYSIS_TOOLS = ["kreo", "lego", "lego+"]


@click.command()
@click.argument("project-name")
def generate_data(project_name: str) -> None:
    env = jinja2.Environment(loader=jinja2.FileSystemLoader(searchpath=str(data_dir)))
    for analysis_tool in ANALYSIS_TOOLS:
        template = env.get_template(f"{analysis_tool}-project.json.j2")
        res = template.render(name=project_name)
        config_json = data_dir / f"{analysis_tool}-{project_name}.json"
        with open(config_json, "w") as f:
            f.write(res)
            LOGGER.warning("Created file %s", config_json)

        analysis_out_dir = data_dir / f"{analysis_tool}-{project_name}"
        analysis_out_dir.mkdir(exist_ok=True)

        template_gitignore = data_dir / ".git-ignore-template"
        shutil.copy(str(template_gitignore), analysis_out_dir / ".gitignore")
        LOGGER.warning("Populated directory %s with gitignore", analysis_out_dir)


if __name__ == "__main__":
    generate_data()
