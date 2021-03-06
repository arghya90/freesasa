use strict;

# Script to generate source code for a classifier based on a
# config-file. Useful for having default classifiers statically in
# memory.  It generates a constant classifier that should be placed in
# its own source-file. 

# This interpreter has stricter requirements on the input format than
# the one in the C-code, since it is only for internal use. It also
# doesn't do any syntax checks, so it will only work on valid config
# files.

# The script should be run from the same directory as it is stored,
# because it depends on the subdir 'rsa/' to gengerate reference SASA
# values for RSA output. The file classifier_protor.c in the
# source-directory was generated using the command
#
#    perl config2c.pl protor ../share/protor.config
# 
# where 'protor' will be the variable name prefix and the name used in
# output generated from this classifier. It is also referred to in
# freesasa.h.

# Ideally RSA values should be stored in the configuration file, in a
# separate section. This will be added in the future.

my %types;
my %atoms;
my %residues;
my %rsa;
my $name;
my $n_classes = 0;
my $n_residues = 0;
my $atom_flag = 0;
my $type_flag = 0;

(scalar @ARGV == 2) or die "usage : $0 <variable_prefix> <config-file>\n";
my $prefix = shift @ARGV;
my $config_file = shift @ARGV;

open(my $input, "<$config_file") or die "Can't open $config_file. $!";

while (<$input>) {
    next if (/^#/);
    chomp;
    $_ =~ s/^(.*)#.*/$1/; # strip comments
    next if (/^\s*$/);
    if (/^types:/) {
        $type_flag = 1;
        $atom_flag = 0;
        next;
    }
    if (/^atoms:/) {
        $type_flag = 0;
        $atom_flag = 1;
        next;
    }
    if (/^name:/) {
        $_ =~ s/name:\s+(\S+)/$1/;
        $name = $_;
        next;
    }
    if ($type_flag) {
        my ($name,$radius,$class) = split /\s+/, $_;
        my $class_code;
        $types{$name}{radius} = $radius;
        if ($class =~ /apolar/i) {$class_code = 'FREESASA_ATOM_APOLAR';}
        elsif ($class =~ /polar/i) {$class_code = 'FREESASA_ATOM_POLAR';}
        else {die "Only classes 'polar' and 'apolar' allowed";}
        $types{$name}{class} = $class_code;
    }
    if ($atom_flag) {
        my ($res,$atom,$type) = split /\s+/, $_;
        $atoms{$res}{$atom} = $type;
        if (! exists $residues{$res}) {
            $residues{$res} = $n_residues;
            ++$n_residues;
        }
    }
}
close($input);

# generate RSA values
my @pdb = `ls rsa/*.pdb`;
foreach my $p (@pdb) {
    chomp $p;
    my $name = substr($p,4,3);
    my @rsa_atoms = `cat $p`;
    my (@pol_atoms, @apol_atoms);
    foreach (@rsa_atoms) {
        next if (! /^ATOM/);
        next if (! (substr($_,25,1) eq '2'));
        next if (/H\s*$/); #skip hydrogen
        my $atom = substr($_,12,4);
        next if (substr($_,1,1) eq "H");
        my $c;
        $atom =~ s/\s//g;
        if (exists $atoms{$name}{$atom}) {
            $c = $types{$atoms{$name}{$atom}}{class};
        } elsif (exists $atoms{'ANY'}{$atom}) {
            $c = $types{$atoms{'ANY'}{$atom}}{class};
        } else {
            die "Atom $name $atom not in classifier";
        }
        push(@apol_atoms, $atom) if ($c =~ /FREESASA_ATOM_APOLAR/);
        push(@pol_atoms,  $atom) if ($c =~ /FREESASA_ATOM_POLAR/);
    }
    my $select_total = '--select="res2, resi 2"';
    my $select_bb = '--select="bb, resi 2 and name c+n+o+ca"';
    my $select_sc = '--select="sc, resi 2 and not name c+n+o+ca"';
    my ($select_apol, $select_pol);
    if (scalar @apol_atoms > 0) {
        $select_apol = '--select="apol, resi 2 and name ' . join('+',@apol_atoms) . '"';
    }
    if (scalar @pol_atoms > 0) {
        $select_pol  = '--select="pol, resi 2 and name '  . join('+',@pol_atoms) . '"';
    }
    my @data = `../src/freesasa $p -c $config_file -n 1000 $select_total $select_bb $select_sc $select_apol $select_pol`;
    my %subarea;
    $subarea{pol} = $subarea{apol} = 0;
    $subarea{name} = $name;
    foreach (@data) {
        if (/^res2 :\ +(\d+.\d+)/) {
            $subarea{total} = $1;
        }
        if (/^bb :\ +(\d+.\d+)/) {
            $subarea{bb} = $1;
        }
        if (/^sc :\ +(\d+.\d+)/) {
            $subarea{sc} = $1;
        }
        if (/^pol :\ +(\d+.\d+)/) {
            $subarea{pol} = $1;
        }
        if (/^apol :\ +(\d+.\d+)/) {
            $subarea{apol} = $1;
        }
    }
    $rsa{$name} = "\{.name = \"$name\", .total = $subarea{total}, .main_chain = $subarea{bb}, ".
        ".side_chain = $subarea{sc}, .polar = $subarea{pol}, .apolar = $subarea{apol}\}";
}

my @res_array = sort keys %residues;
print "#include \"classifier.h\"\n\n";
print "/* Autogenerated code from the script config2c.pl */\n\n";
print "static const char *$prefix\_residue_name[] = {";
print "\"$_\", "foreach (@res_array);
print "};\n";

foreach my $res (@res_array) {
    my @atom_names = keys %{$atoms{$res}};
    print "static const char *$prefix\_$res\_atom_name[] = {";
    print "\"$_\", " foreach (@atom_names);
    print "};\n";
    print "static double $prefix\_$res\_atom_radius[] = {";
    print $types{$atoms{$res}{$_}}{radius},", " foreach (@atom_names);
    print "};\n";
    print "static int $prefix\_$res\_atom_class[] = {";
    print $types{$atoms{$res}{$_}}{class},", " foreach (@atom_names);
    print "};\n";
    print "static struct classifier_residue $prefix\_$res\_cfg = {\n";
    print "    .name = \"$res\", .n_atoms = ", scalar keys %{$atoms{$res}},",\n";
    print "    .atom_name = (char**) $prefix\_$res\_atom_name,\n";
    print "    .atom_radius = (double*) $prefix\_$res\_atom_radius,\n";
    print "    .atom_class = (freesasa_atom_class*) $prefix\_$res\_atom_class,\n";
    if (exists $rsa{$res}) {
        print "    .max_area = $rsa{$res},";
    } else {
        print "    .max_area = {NULL, 0, 0, 0, 0, 0},";
    }
    print"\n};\n\n"
}
print "static struct classifier_residue *$prefix\_residue_cfg[] = {\n    ";
foreach my $res (@res_array) {
    print "&$prefix\_$res\_cfg, ";
}
print "};\n\n";


print "const freesasa_classifier freesasa_$prefix\_classifier = {\n";
print "    .n_residues = $n_residues,";
print "    .residue_name = (char**) $prefix\_residue_name,\n";
print "    .residue = (struct classifier_residue **) $prefix\_residue_cfg,\n";
print "    .name = \"$name\",\n";
print "};\n\n";


      
