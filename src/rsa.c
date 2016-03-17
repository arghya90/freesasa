#include "freesasa.h"
#include "freesasa_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <pdb.h>
#if HAVE_CONFIG_H
#  include <config.h>
#endif

struct residue_sasa {
    const char *name;
    double total;
    double main_chain;
    double side_chain;
    double polar;
    double apolar;
};

static const struct residue_sasa zero_rs = {NULL, 0, 0, 0, 0, 0};

static const struct residue_sasa rsa_sasa_ref[] = {
    {.name = "ALA", .total = 103.10, .main_chain = 46.51, .side_chain = 56.60, .polar = 29.89, .apolar = 73.21},
    {.name = "CYS", .total = 125.02, .main_chain = 45.47, .side_chain = 79.55, .polar = 79.68, .apolar = 45.33},
    {.name = "ASP", .total = 135.76, .main_chain = 44.65, .side_chain = 91.11, .polar = 88.93, .apolar = 46.83},
    {.name = "GLU", .total = 167.95, .main_chain = 45.12, .side_chain = 122.83, .polar = 113.74, .apolar = 54.21},
    {.name = "PHE", .total = 193.68, .main_chain = 43.52, .side_chain = 150.16, .polar = 29.89, .apolar = 163.79},
    {.name = "GLY", .total = 71.84, .main_chain = 71.84, .side_chain = 0.00, .polar = 31.58, .apolar = 40.26},
    {.name = "HIS", .total = 173.43, .main_chain = 44.26, .side_chain = 129.18, .polar = 69.25, .apolar = 104.18},
    {.name = "ILE", .total = 167.30, .main_chain = 39.09, .side_chain = 128.22, .polar = 24.70, .apolar = 142.60},
    {.name = "LYS", .total = 197.47, .main_chain = 45.10, .side_chain = 152.38, .polar = 87.44, .apolar = 110.04},
    {.name = "LEU", .total = 160.87, .main_chain = 44.85, .side_chain = 116.01, .polar = 29.89, .apolar = 130.98},
    {.name = "MET", .total = 185.43, .main_chain = 45.08, .side_chain = 140.35, .polar = 67.61, .apolar = 117.83},
    {.name = "ASN", .total = 138.45, .main_chain = 43.80, .side_chain = 94.65, .polar = 93.13, .apolar = 45.32},
    {.name = "PRO", .total = 132.32, .main_chain = 29.83, .side_chain = 102.49, .polar = 16.16, .apolar = 116.16},
    {.name = "GLN", .total = 172.69, .main_chain = 45.09, .side_chain = 127.60, .polar = 123.13, .apolar = 49.56},
    {.name = "ARG", .total = 231.99, .main_chain = 45.09, .side_chain = 186.90, .polar = 153.92, .apolar = 78.07},
    {.name = "SER", .total = 111.49, .main_chain = 46.10, .side_chain = 65.39, .polar = 58.63, .apolar = 52.86},
    {.name = "THR", .total = 133.09, .main_chain = 40.38, .side_chain = 92.71, .polar = 49.91, .apolar = 83.18},
    {.name = "VAL", .total = 146.72, .main_chain = 44.24, .side_chain = 102.48, .polar = 29.89, .apolar = 116.83},
    {.name = "TRP", .total = 226.55, .main_chain = 40.50, .side_chain = 186.05, .polar = 61.19, .apolar = 165.37},
    {.name = "TYR", .total = 208.08, .main_chain = 43.49, .side_chain = 164.58, .polar = 76.46, .apolar = 131.62},
    {NULL, 0, 0, 0, 0, 0}, // marks end of array
};
/**
    Returns 1 if the atom_name equals CA, N, O or C after whitespace
    is trimmed, 0 else. (i.e. does not check if it is an actual atom,
    no such strings should be able to reach this point).
 */
static int
rsa_atom_is_backbone(const char *atom_name)
{
    assert(strlen(atom_name) <= PDB_ATOM_NAME_STRL);

    char name[PDB_ATOM_NAME_STRL] = "";
    sscanf(atom_name, "%s", name); //trim whitespace

    if (strlen(name) == 0) return 0;
    if (strcmp(name, "CA") == 0 ||
        strcmp(name, "N") == 0 ||
        strcmp(name, "O") == 0 ||
        strcmp(name, "C") == 0)
        return 1;
    return 0;
}

/**
    Returns 0 if the first non-whitespace charactor of atom_name is
    'C' or if it is only whitespace/empty, 1 else. (i.e. does not
    check if it is an actual atom, no such strings should be able to
    reach this point).
 */
static int
rsa_atom_is_polar(const char *atom_name)
{
    assert(strlen(atom_name) <= PDB_ATOM_NAME_STRL);

    char name[PDB_ATOM_NAME_STRL] = "";
    sscanf(atom_name,"%s", name); //trim whitespace

    if (strlen(name) == 0) return 0;
    else if (name[0] == 'C') return 0;
    return 1;
}

/**
   Adds v to members of rs depending on how the atom specified by resn
   and atom_name is classified. If the classifiers are null the
   functions rsa_atom_is_backbone() and rs_atom_is_polar() are used
   instead.
 */
static inline void
rsa_abs_add_atom(struct residue_sasa *rs,
                 double v,
                 const char *resn,
                 const char *atom_name,
                 const freesasa_classifier *p_classifier,
                 const freesasa_classifier *bb_classifier)
{
    rs->total += v;
    if (!bb_classifier) {
        if (rsa_atom_is_backbone(atom_name)) rs->main_chain += v;
        else rs->side_chain += v;
    } else {
        if (bb_classifier->sasa_class(resn, atom_name, bb_classifier))
            rs->main_chain += v;
        else rs->side_chain += v;
    }
    if (!p_classifier){
        if (rsa_atom_is_polar(atom_name)) rs->polar += v;
        else rs->apolar += v;
    } else {
        if (p_classifier->sasa_class(resn, atom_name, p_classifier))
            rs->polar += v;
        else rs->apolar += v;
    }
}

/**
    Get the absolute SASA values of residue idx in structure.
 */
static int
rsa_get_abs(struct residue_sasa *rs,
            int idx,
            const char *resn,
            const freesasa_result *result,
            const freesasa_structure *structure,
            const freesasa_classifier *p_classifier,
            const freesasa_classifier *bb_classifier)

{
    char selbuf[100];
    char namebuf[FREESASA_MAX_SELECTION_NAME];
    int first, last;

    *rs = zero_rs;
    if (freesasa_structure_residue_atoms(structure, idx, &first, &last))
        return fail_msg("");
    rs->name = resn;

    for (int i = first; i <= last; ++i) {
        double v = result->sasa[i];
        const char *name = freesasa_structure_atom_name(structure,i);
        assert(name);
        rsa_abs_add_atom(rs, v, resn, name, p_classifier, bb_classifier);
    }

    return FREESASA_SUCCESS;
}

/**
    Calculate relative sasa values based on abs and ref, store in rel.
 */
static int
rsa_get_rel(struct residue_sasa *rel,
            const struct residue_sasa *abs,
            const struct residue_sasa *ref)
{
    int i_ref = -1;
    double nan = 0.0/0.0;

    for(int j = 0; ref[j].name != NULL; ++j) {
        if (strcmp(ref[j].name, abs->name) == 0) {
            i_ref = j;
            break;
        }
    }

    if (i_ref >= 0) {
        rel->total = 100. * abs->total / ref[i_ref].total;
        rel->side_chain = 100. * abs->side_chain / ref[i_ref].side_chain;
        rel->main_chain = 100. * abs->main_chain / ref[i_ref].main_chain;
        rel->polar = 100. * abs->polar / ref[i_ref].polar;
        rel->apolar = 100. * abs->apolar / ref[i_ref].apolar;
    } else {
        rel->total = rel->side_chain = rel->main_chain = rel->polar = rel->apolar = nan;
    }

    return i_ref;
}

/**
    Add members of term to members of sum
 */
static void
rsa_add_residue_sasa(struct residue_sasa *sum,
                     const struct residue_sasa *term)
{
    sum->total += term->total;
    sum->side_chain += term->side_chain;
    sum->main_chain += term->main_chain;
    sum->polar += term->polar;
    sum->apolar += term->apolar;
}

static void
rsa_print_header(FILE *output,
                 const char *name)
{
#ifdef PACKAGE_VERSION
    fprintf(output, "REM  FreeSASA " PACKAGE_VERSION "\n");
#else
    fprintf(output, "REM  FreeSASA\n");
#endif
    fprintf(output, "REM  Using default relative accessibilites\n");
    fprintf(output, "REM  Absolute and relative SASAs for %s\n", name);
    fprintf(output, "REM RES _ NUM      All-atoms   Total-Side   Main-Chain    Non-polar    All polar\n");
    fprintf(output, "REM                ABS   REL    ABS   REL    ABS   REL    ABS   REL    ABS   REL\n");
}

static inline void 
rsa_print_abs_rel(FILE*output,
                  double abs,
                  double rel)
{
    fprintf(output, "%7.2f", abs);
    if (isfinite(rel)) fprintf(output, "%6.1f", rel);
    else fprintf(output, "   N/A");
}

static void
rsa_print_residue(FILE *output, 
                  const struct residue_sasa *abs,
                  const struct residue_sasa *rel,
                  int i_ref)
{
    rsa_print_abs_rel(output, abs->total, rel->total);
    rsa_print_abs_rel(output, abs->side_chain, rel->side_chain);
    rsa_print_abs_rel(output, abs->main_chain, rel->main_chain);
    rsa_print_abs_rel(output, abs->apolar, rel->apolar);
    rsa_print_abs_rel(output, abs->polar, rel->polar);
    fprintf(output, "\n");
}

int
freesasa_rsa_print(FILE *output,
                   const freesasa_result *result,
                   const freesasa_structure *structure,
                   const char *name,
                   FILE *reference,
                   const freesasa_classifier *polar_classifier,
                   const freesasa_classifier *backbone_classifier)
{
    assert(output);
    assert(result);
    assert(structure);
    assert(name);

    const char *chain_labels = freesasa_structure_chain_labels(structure);
    int naa = freesasa_structure_n_residues(structure), first, last, resi, 
        i_ref, n_chains = strlen(chain_labels);
    char chain;
    const char *resi_str, *resn;
    struct residue_sasa abs, rel, chain_abs[n_chains], all_chains_abs = zero_rs;
    const struct residue_sasa *sasa_ref;
    const freesasa_classifier *p_classifier = polar_classifier,
        *bb_classifier = backbone_classifier;

    if (reference == NULL) sasa_ref = rsa_sasa_ref;
    else return fail_msg("Reference SASA values from file not implemented yet");

    for (int i = 0; i < n_chains; ++i) chain_abs[i] = zero_rs;
    
    rsa_print_header(output, name);

    for (int i = 0; i < naa; ++i) {
        abs = rel = zero_rs;
        freesasa_structure_residue_atoms(structure, i, &first, &last);
        resi_str = freesasa_structure_atom_res_number(structure, first);
        resn = freesasa_structure_atom_res_name(structure, first);
        chain = freesasa_structure_atom_chain(structure, first);
        resi = atoi(resi_str);
             
        if (rsa_get_abs(&abs, i, resn, result, structure, p_classifier, bb_classifier))
            return fail_msg("");
        i_ref = rsa_get_rel(&rel, &abs, sasa_ref);

        rsa_add_residue_sasa(&all_chains_abs, &abs);

        for (int j = 0; j < n_chains; ++j) {
            if (chain_labels[j] == chain) {
                rsa_add_residue_sasa(&chain_abs[j], &abs);
            }
        }

        fprintf(output, "RES %s %c%s  ", abs.name, chain, resi_str);
        rsa_print_residue(output, &abs, &rel, i_ref);
    }
    
    fprintf(output, "END  Absolute sums over single chains surface\n");
    for (int i = 0; i < n_chains; ++i) {
        fprintf(output,"CHAIN%3d %c %10.1f   %10.1f   %10.1f   %10.1f   %10.1f\n",
                i+1, chain_labels[i], chain_abs[i].total, chain_abs[i].side_chain,
                chain_abs[i].main_chain, chain_abs[i].apolar, chain_abs[i].polar);
    }
    fprintf(output, "END  Absolute sums over all chains\n");
    fprintf(output,"TOTAL      %10.1f   %10.1f   %10.1f   %10.1f   %10.1f\n",
            all_chains_abs.total, all_chains_abs.side_chain,
            all_chains_abs.main_chain, all_chains_abs.apolar, all_chains_abs.polar);
    
    fflush(output);
    if (ferror(output)) {
        return fail_msg(strerror(errno));
    }
    
    return FREESASA_SUCCESS;
}