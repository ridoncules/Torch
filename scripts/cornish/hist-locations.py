import sys
import warnings
import numpy as np

# Parse arguments.
import argparse
parser = argparse.ArgumentParser(description='Calculates max flux in cornish set.')
parser.add_argument('iden', metavar='iden', type=int, help='Density index in CORNISH param set.')
parser.add_argument('-ben', action='store_true', help='Use simple ben stromgren survey.')
parser.add_argument('-harry', action='store_true', help='Use simple harry stromgren survey.')
args = parser.parse_args()

def load_src(name, fpath):
    import os, imp
    return imp.load_source(name, os.path.join(os.path.dirname(__file__), fpath))

load_src("torch", "../torchpack/torch.py")
load_src("hgspy", "../torchpack/hgspy.py")

import torch
import hgspy

load_src("cdat", "../torchpack/cornishdata.py")
import cdat
cornish_data = cdat.CornishData(args.iden)
star_data = cornish_data.star_data
nstars = len(star_data[:,0])

DPI = 300
figformat = 'png'
plot_size = 4.0
fontsize = 13
torch.set_font_sizes(fontsize)

benstr = ""
if args.ben:
	benstr = "-ben"
if args.harry:
	benstr = "-harry"
outputfile = cornish_data.dirname + '/hist-locations' + benstr + '.' + figformat

### Data
simulated_survey = np.genfromtxt(cornish_data.dirname + "/final-survey" + benstr + ".txt", skip_header=1)
cornish_survey = np.genfromtxt("data/cornish/cornish-uchiis.txt", skip_header=1, delimiter=',')
cornish_distances = np.genfromtxt("data/cornish/cornish-distances.txt", skip_header=1)

### Plotting.
plotter = torch.Plotter(1, 1, plot_size, figformat, DPI)
plotter.ticklength *= 0.5

###	Axes.
asp_rat = 1.0
grid = plotter.axes1D((1,3), aspect_ratio=asp_rat)

grid[0].set_xlabel(plotter.format_label(torch.VarType('l', units='deg')))
grid[1].set_xlabel(plotter.format_label(torch.VarType('b', units='deg')))
grid[2].set_xlabel(plotter.format_label(torch.VarType('d', units='kpc')))

grid[0].set_ylabel(plotter.format_label(torch.VarType('N')))

grid[0].set_xlim([10.0, 65.0])
grid[1].set_xlim([-1, 1])
grid[2].set_xlim([0.0, 20.0])

ymax = 70
nyticks = 7

grid[0].set_ylim([0, ymax])
grid[1].set_ylim([0, ymax])
grid[2].set_ylim([0, ymax])

for icol in range(3):
	grid[icol].set_yticks(np.arange(0, ymax +  (ymax - 1)/ float(nyticks), ymax / float(nyticks)))

kx1 = dict(linewidth=1.5, label="CORNISH", color='b')
kx2 = dict(linewidth=1.5, label="Simulated", color='r', linestyle='--')

bins0 = np.arange(10, 65, 2)
bins1 = np.arange(-1, 1, 0.1)
bins2 = np.arange(0, 20, 1)

bincentres0 = 0.5 * (bins0[:-1] + bins0[1:])
bincentres1 = 0.5 * (bins1[:-1] + bins1[1:])
bincentres2 = 0.5 * (bins2[:-1] + bins2[1:])

normed = False

plotter.histstep(grid[0], cornish_survey[:,1], bins0, normed=normed, errorcentres=bincentres0, **kx1)
plotter.histstep(grid[0], simulated_survey[:,11], bins0, normed=normed, **kx2)
plotter.histstep(grid[1], cornish_survey[:,2], bins1, normed=normed, errorcentres=bincentres1, **kx1)
plotter.histstep(grid[1], simulated_survey[:,12], bins1, normed=normed, **kx2)
plotter.histstep(grid[2], cornish_distances[:,1], bins2, normed=normed, errorcentres=bincentres2, **kx1)
plotter.histstep(grid[2], simulated_survey[:,4], bins2, normed=normed, **kx2)

### Legend
handles, labels = grid[2].get_legend_handles_labels()
legend = grid[2].legend(handles, labels, loc=1)
legend.get_frame().set_linewidth(plotter.linewidth)

###	Save figure.
with warnings.catch_warnings():
	warnings.simplefilter("ignore")
	plotter.save_plot(outputfile)

print sys.argv[0] + ': plotted in ' + outputfile