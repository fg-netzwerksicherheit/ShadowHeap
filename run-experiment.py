#!/usr/bin/env python3

import json
import os
import random
import re
import subprocess
import sys
import threading
import typing as T


def msg_please_install(*libs):
    print("Please install the following Python 3 libraries:", ", ".join(libs),
          file=sys.stderr)


try:
    import click
except ImportError:
    msg_please_install('click')
    raise


@click.group()
def cli():
    """Manage LD_PRELOAD experiments."""
    pass


@cli.command()
@click.option(
    '--verbose/--no-verbose', default=False,
    help="Show progress messages during execution.")
@click.option(
    '--preload', type=(str, str), metavar="NAME 'LIBRARY... [ENV=VALUE...]'",
    multiple=True, required=True,
    help="Declare an experiment to preload the given library, "
    "possibly setting extra variables.")
@click.option(
    '--repetitions', type=int, metavar='N', default=1,
    help="Repeat each experiment N times.")
@click.option(
    '--database', 'database_file', metavar='FILE',
    type=click.File(mode='a+'), required=True,
    help="Store experiment results in this file, allows resumption")
@click.option(
    '-e', '--env', 'env', metavar='NAME=VALUE',
    help="add environment variables",
    multiple=True)
@click.option(
    '--envvar-instead-of-ld-preload', type=str, metavar='NAME',
    help="let a subprocess set the LD_PRELOAD env var instead",
    default=None)
@click.argument('command', metavar='COMMAND', nargs=-1, required=True)
def run(
    command, preload, *,
    verbose, repetitions, database_file, env, envvar_instead_of_ld_preload):
    """
    Run the COMMAND under each of the preload experiments.

    Example invocation:

        \b
        run_experiments.py run --verbose --repetitions 5 --output log.txt \\
            --preload none '' \\
            --preload hooked \\
                'bin/malloc-hooked.so' \\
            --preload shadow \\
                'bin/malloc-shadow.so' \\
            --preload shadow-usb \\
                'bin/malloc-shadow.so SHADOWHEAP_DISABLE_USBCHECKS=0' \\
            -- bin/some-example-program --with arguments
    """

    # initialize the experiment database
    database_file.seek(0)
    db = ExperimentDatabase()
    db.load_json(database_file)

    # prepare the experiment schedule
    schedule = list(calculate_experiment_schedule(
        existing_results_in_database=lambda experiment:
            len(list(db.where_name_is(experiment))),
        repetitions=repetitions,
        experiments=preload,
    ))
    random.shuffle(schedule)

    if verbose:
        print("scheduled {} experiments, with {} in the database".format(
            len(schedule), len(list(db.all()))))

    # set up env
    env = {key: value
           for assignment in env
           for key, value in [assignment.split('=', 1)]}

    library_env_var = 'LD_PRELOAD'
    if envvar_instead_of_ld_preload is not None:
        library_env_var = envvar_instead_of_ld_preload

    for (counter, (name, libraries, extra_env)) in enumerate(schedule):

        if verbose:
            print(
                "running experiment {}/{}: {}".format(
                    counter, len(schedule), name),
                file=sys.stderr)

        perf_data = capture_performance(command, env={
            **env,
            **extra_env,
            library_env_var: ' '.join(libraries),
        })
        db.insert(**perf_data, name=name)

        db.sync_json(database_file)


@cli.command()
@click.argument('database_file', metavar='DATABASE',
                type=click.File('r'), required=True)
@click.argument('csv_file', metavar='[OUTPUT]',
                type=click.File('w'), default='-')
def convert_to_csv(database_file, csv_file):
    """
    Convert the JSON database to a CSV file.

    The file contains the following columns:

        \b
        key            type   unit description
        -------------- ------ ---- ---------------------------------
        name           string --   name of the experiment configuration
        real           float  sec  elapsed wall time
        user           float  sec  elapsed CPU time in user mode
        sys            float  sec  elapsed CPU time in kernel mode
        mem_total_avg  int    KB   typical memory consumption (unreliable)
        mem_max        int    KB   maximum resident memory
        exit           int    --   exit status, should be zero
    """

    import csv

    db = ExperimentDatabase()
    db.load_json(database_file)

    columns, data = db.to_array()

    writer = csv.writer(csv_file)
    writer.writerow(columns)
    writer.writerows(data)


@cli.command()
@click.option(
    '--database', 'database_file', metavar='FILE',
    type=click.File('a+'), required=True,
    help="The database which should be manipulated.")
@click.option(
    '--verbose/--no-verbose',
    help="Verbose mode.")
@click.argument('experiments', metavar='NAME', nargs=-1, required=True)
def delete_experiment(*, verbose, database_file, experiments):
    """
    Delete results for the given experiments from the database.

    The experiment names can also be given as a glob pattern,
    e.g. “foo-*” will delete “foo-bar” as well.
    """

    experiments = [regexify_glob(name) for name in experiments]

    def match_any_experiment(name: str) -> bool:
        return any(pattern.match(name) for pattern in experiments)

    database_file.seek(0)
    db = ExperimentDatabase()
    db.load_json(database_file)

    count = sum(1 for _ in db.delete_where(
        lambda x: match_any_experiment(x.name)
    ))

    db.sync_json(database_file)

    if verbose:
        print("deleted {} results".format(count), file=sys.stderr)


def regexify_glob(glob_pattern: str):
    r"""Basically just replace ``*`` with ``.*``.

    >>> print(regexify_glob(r'foo?-*-\bar*').pattern)
    foo\?\-.*\-\\bar.*
    """
    raw = '.*'.join(re.escape(part) for part in glob_pattern.split('*'))
    return re.compile(raw)


@cli.command()
@click.option(
    '-o', '--output', 'outfile',
    type=click.Path(), required=True,
    help="write a PDF plot to this file.")
@click.option(
    '--database', 'database_file', metavar='DATABASE',
    type=click.File('r'), required=True,
    help="Database that contains experiment results.")
@click.argument('columns', nargs=-1, required=True)
def plot(*, outfile, database_file, columns):
    """
    Draw boxplots with the data from one or more  COLUMNS.
    """

    # load required modules
    try:
        import pandas as pd
        import matplotlib.pyplot as plt
        import seaborn as sns
    except ImportError:
        msg_please_install('pandas', 'matplotlib', 'seaborn')
        raise

    # load the data
    db = ExperimentDatabase()
    db.load_json(database_file)
    col_names, raw_data = db.to_array()
    data = pd.DataFrame.from_records(raw_data, columns=col_names)
    data.sort_values(by='name', inplace=True)

    # create the boxplots
    fig, axes = plt.subplots(len(columns), 1, sharey=True, squeeze=False)
    for i, col in enumerate(columns):
        ax = axes[i][0]
        sns.boxplot(y=data['name'], x=data[col], ax=ax)
        ax.set_xlim(0, None)
    plt.savefig(outfile)

    # generate summary statistics for each metric
    grouped = data.groupby(['name']).describe()
    if tuple(int(x) for x in pd.__version__.split('.')) >= (0, 23, 0):
        for col in grouped.columns.levels[0]:
            print("Summary for metric `{}`:".format(col))
            print(grouped.loc[:, col])


class ExperimentRecord:
    def __init__(self, name, *, real, user, sys, mem_total_avg, mem_max, exit):
        self.name = name
        self.real = real
        self.user = user
        self.sys = sys
        self.mem_total_avg = mem_total_avg
        self.mem_max = mem_max
        self.exit = exit

    def to_dict(self):
        """Provide the record entries as a normal dictionary.

        >>> kwargs = dict(name='base', real=1.3, user=0.9, sys=0.2,
        ... mem_total_avg=0, mem_max=1200, exit=0)
        >>> assert ExperimentRecord(**kwargs).to_dict() == kwargs
        """
        return dict(
            name=self.name,
            real=self.real,
            user=self.user,
            sys=self.sys,
            mem_total_avg=self.mem_total_avg,
            mem_max=self.mem_max,
            exit=self.exit,
        )


class ExperimentDatabase:
    """
    >>> # store data in a database
    >>> db = ExperimentDatabase()
    >>> import io; file = io.StringIO('[{"old": "contents"}]')
    >>> record = dict(name='base', real=1.1, user=0.9, sys=0.2,
    ... mem_total_avg=0, mem_max=1234, exit=1)
    >>> db.insert(**record)
    >>> db.sync_json(file)
    >>> # restore state in a new database
    >>> _ = file.seek(0)
    >>> db = ExperimentDatabase()
    >>> db.load_json(file)
    >>> assert next(db.all()).to_dict() == record
    >>> print(file.getvalue())  # doctest: +ELLIPSIS
    [{...}]
    """
    def __init__(self):
        self._data = []

    def load_json(self, reader):
        contents = reader.read()
        if not contents:
            return
        for record in json.loads(contents):
            self._data.append(ExperimentRecord(**record))

    def sync_json(self, writer):
        writer.seek(0)
        writer.truncate()
        self.dump_json(writer)

    def dump_json(self, writer):
        json.dump([record.to_dict() for record in self._data], writer)

    def all(self) -> T.Iterable[ExperimentRecord]:
        yield from self._data

    def insert(self, **kwargs):
        self._data.append(ExperimentRecord(**kwargs))

    def where(self, query: callable) -> T.Iterable[ExperimentRecord]:
        for record in self._data:
            if query(record):
                yield record

    def where_name_is(self, name: str) -> T.Iterable[ExperimentRecord]:
        for record in self._data:
            if record.name == name:
                yield record

    def delete_where(self, query) -> T.Iterable[ExperimentRecord]:
        keep = []
        for record in self._data:
            if query(record):
                yield record
            else:
                keep.append(record)
        self._data[:] = keep

    def to_array(self) -> T.Tuple[T.List[str], T.List[tuple]]:
        columns = list('name real user sys mem_total_avg mem_max exit'.split())
        data = []
        for record in self._data:
            data.append((
                record.name,
                record.real, record.user, record.sys,
                record.mem_total_avg, record.mem_max,
                record.exit,
            ))

        return columns, data


def calculate_experiment_schedule(
        *,
        existing_results_in_database: T.Callable[[str], int],
        repetitions: int,
        experiments: T.Iterable[T.Tuple[str, str]]):
    """
    Figure out which experiments need to be run in which configuration.

    >>> database = dict(foo=3)
    >>> list(calculate_experiment_schedule(
    ...     existing_results_in_database=lambda experiment:
    ...         database.get(experiment, 0),
    ...     repetitions=5,
    ...     experiments=[
    ...        ('foo', 'libfoo.so\tlibquz.so'),
    ...        ('bar', 'libbar.so x=y   z=123'),
    ...     ],
    ... ))  # doctest: +NORMALIZE_WHITESPACE
    [('foo', ['libfoo.so', 'libquz.so'], {}),
     ('foo', ['libfoo.so', 'libquz.so'], {}),
     ('bar', ['libbar.so'], {'x': 'y', 'z': '123'}),
     ('bar', ['libbar.so'], {'x': 'y', 'z': '123'}),
     ('bar', ['libbar.so'], {'x': 'y', 'z': '123'}),
     ('bar', ['libbar.so'], {'x': 'y', 'z': '123'}),
     ('bar', ['libbar.so'], {'x': 'y', 'z': '123'})]
    """

    for (experiment, items) in experiments:
        existing_results = existing_results_in_database(experiment)

        if existing_results >= repetitions:
            continue

        extra_env = {}
        libraries = []
        for item in items.split():
            if '=' in item:
                name, value = item.split('=')
                extra_env[name] = value
            else:
                libraries.append(item)

        for _ in range(repetitions - existing_results):
            yield (experiment, libraries, extra_env)


TIME_FORMAT = """
{"real": %e, "user": %U, "sys": %S,
 "mem_total_avg": %K, "mem_max": %M,
 "exit": %x}
"""


def capture_performance(command, *, env=dict()):
    import tempfile

    with tempfile.NamedTemporaryFile() as time_data:

      command = [
          'time',
          '--format', TIME_FORMAT,
          '--output', time_data.name,
          'env',
          *('{}={}'.format(key, value) for key, value in env.items()),
          *command,
      ]

      subprocess.run(command, check=True)

      time_data.seek(0)
      perf_data = time_data.read()

    return json.loads(perf_data)


if __name__ == '__main__':
    cli()
