/****************************************************************************
 * Copyright (C) from 2009 to Present EPAM Systems.
 *
 * This file is part of Indigo toolkit.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ***************************************************************************/

#include <cctype>
#include <memory>
#include <regex>
#include <unordered_set>

#include "base_cpp/scanner.h"
#include "layout/molecule_layout.h"
#include "layout/sequence_layout.h"
#include "molecule/molecule.h"
#include "molecule/monomer_commons.h"
#include "molecule/sequence_loader.h"

using namespace indigo;

IMPL_ERROR(SequenceLoader, "SEQUENCE loader");

SequenceLoader::SequenceLoader(Scanner& scanner)
    : _scanner(scanner), _mon_lib(MonomerTemplates::_instance()), _seq_id(0), _last_monomer_idx(-1), _row(-1), _col(0)
{
}

SequenceLoader::~SequenceLoader()
{
}

void SequenceLoader::loadFasta(BaseMolecule& mol, const std::string& seq_type_str)
{
    if (seq_type_str == kMonomerClassDNA)
        loadFasta(mol, SeqType::DNASeq);
    else if (seq_type_str == kMonomerClassRNA)
        loadFasta(mol, SeqType::RNASeq);
    else if (seq_type_str == kMonomerClassPEPTIDE)
        loadFasta(mol, SeqType::PEPTIDESeq);
    else
        throw Error("Bad sequence type: %s", seq_type_str.c_str());
}

void SequenceLoader::loadFasta(BaseMolecule& mol, SeqType seq_type)
{
    _seq_id = 0;
    _last_monomer_idx = -1;
    _row = 0;
    _col = 0;
    const int row_size = seq_type == SeqType::PEPTIDESeq ? 1 : 2;
    int frag_idx = 0;
    std::string invalid_symbols;
    Array<int> mapping;
    PropertiesMap properties;

    while (!_scanner.isEOF())
    {
        Array<char> str;
        _scanner.readLine(str, true);
        if (str.size())
        {
            std::string fasta_str = str.ptr();
            switch (fasta_str.front())
            {
            case ';':
                // handle comment
                continue;
                break;
            case '>':
                // handle header
                if (_seq_id)
                {
                    _seq_id = 0;
                    _col = 0;
                    _row += row_size;
                }
                _last_monomer_idx = -1;
                properties.insert(kFASTA_HEADER, fasta_str);
                if (mol.vertexCount() > 0) // do not increment fragment id if first fragment
                    frag_idx++;
                continue;
                break;
            default:
                break;
            }

            for (auto ch : fasta_str)
            {
                if (ch == '-')
                    continue;
                else if (ch == '*' && seq_type == SeqType::PEPTIDESeq && mol.vertexCount())
                {
                    _seq_id = 0;
                    _col = 0;
                    _row += row_size;
                    continue;
                }
                else if (!addMonomer(mol, ch, seq_type))
                {
                    if (invalid_symbols.size())
                        invalid_symbols += ',';
                    invalid_symbols += ch;
                }
            }

            if (invalid_symbols.size())
                throw Error("SequenceLoader::loadFasta(), Invalid symbols in the sequence: %s", invalid_symbols.c_str());

            if (!properties.is_empty())
            {
                mol.properties().insert(frag_idx).copy(properties);
                properties.clear();
            }
        }
    }

    if (!mol.properties().size())
        throw Error("Invalid FASTA: no '>' headers");
}

void SequenceLoader::loadSequence(BaseMolecule& mol, const std::string& seq_type_str)
{
    if (seq_type_str == kMonomerClassDNA)
        loadSequence(mol, SeqType::DNASeq);
    else if (seq_type_str == kMonomerClassRNA)
        loadSequence(mol, SeqType::RNASeq);
    else if (seq_type_str == kMonomerClassPEPTIDE)
        loadSequence(mol, SeqType::PEPTIDESeq);
    else
        throw Error("Bad sequence type: %s", seq_type_str.c_str());
}

void SequenceLoader::loadSequence(BaseMolecule& mol, SeqType seq_type)
{
    _seq_id = 0;
    _last_monomer_idx = -1;
    _row = 0;
    _col = 0;
    const int row_size = seq_type == SeqType::PEPTIDESeq ? 1 : 2;
    mol.clear();
    std::string invalid_symbols;

    bool isGenBankPept = false;
    bool start_char = true;

    while (!_scanner.isEOF())
    {
        auto ch = _scanner.readChar();
        if (ch == '\n' || ch == '\r')
            continue;

        if (start_char)
        {
            if (ch >= NUM_BEGIN && ch < NUM_END)
            {
                isGenBankPept = true;
            }
            start_char = false;
        }

        if (isGenBankPept)
        {
            if (ch == ' ' || (ch >= NUM_BEGIN && ch < NUM_END))
            {
                continue;
            }
            if (ch >= CHAR_LOWERCASE_BEGIN && ch < CHAR_LOWERCASE_END)
            {
                ch -= CHAR_SHIFT_CONVERT;
            }
        }

        if (!isGenBankPept && ch == ' ')
        {
            _seq_id = 0;
            _col = 0;
            _row += row_size;
            continue;
        }
        if (!addMonomer(mol, ch, seq_type))
        {
            if (invalid_symbols.size())
                invalid_symbols += ',';
            invalid_symbols += ch;
        }
    }

    if (invalid_symbols.size())
        throw Error("SequenceLoader::loadSequence(), Invalid symbols in the sequence: %s", invalid_symbols.c_str());
}

bool SequenceLoader::addTemplate(BaseMolecule& mol, const std::string alias, MonomerClass mon_type)
{
    int tg_idx = mol.tgroups.addTGroup();
    auto& tg = mol.tgroups.getTGroup(tg_idx);

    if (_mon_lib.getMonomerTemplate(mon_type, alias, tg))
    {
        tg.tgroup_id = tg_idx;
        _added_templates.emplace(mon_type, alias);
        return true;
    }
    return false;
}

bool SequenceLoader::addMonomer(BaseMolecule& mol, char ch, SeqType seq_type)
{
    MonomerClass mt = seq_type == SeqType::PEPTIDESeq ? MonomerClass::AminoAcid : MonomerClass::Base;
    if (_added_templates.count(std::make_pair(mt, std::string(1, ch))) == 0 && !addTemplate(mol, std::string(1, ch), mt))
        return false;

    if (seq_type != SeqType::PEPTIDESeq)
    {
        // add sugar template
        if (_seq_id == 0)
            addMonomerTemplate(mol, MonomerClass::Sugar, seq_type == SeqType::RNASeq ? "R" : "dR");

        // add phosphate template
        if (_seq_id == 1)
            addMonomerTemplate(mol, MonomerClass::Phosphate, "P");
    }

    _seq_id++;
    switch (seq_type)
    {
    case SeqType::PEPTIDESeq:
        addAminoAcid(mol, ch);
        break;
    case SeqType::RNASeq:
        addNucleotide(mol, std::string(1, ch), "R", "P");
        break;
    case SeqType::DNASeq:
        addNucleotide(mol, std::string(1, ch), "dR", "P");
        break;
    }
    _col++;
    return true;
}

void SequenceLoader::addAminoAcid(BaseMolecule& mol, char ch)
{
    Vec3f pos(_col * MoleculeLayout::DEFAULT_BOND_LENGTH, -MoleculeLayout::DEFAULT_BOND_LENGTH * _row, 0);
    std::string aa(1, ch);
    int amino_idx = mol.asMolecule().addAtom(-1);
    mol.asMolecule().setTemplateAtom(amino_idx, monomerNameByAlias(kMonomerClassAA, aa).c_str());
    mol.asMolecule().setTemplateAtomClass(amino_idx, kMonomerClassAA);
    mol.asMolecule().setTemplateAtomSeqid(amino_idx, _seq_id);
    mol.asMolecule().setAtomXyz(amino_idx, pos);

    if (_seq_id > 1)
    {
        mol.asMolecule().addBond_Silent(amino_idx - 1, amino_idx, BOND_SINGLE);
        mol.setTemplateAtomAttachmentOrder(amino_idx - 1, amino_idx, kRightAttachmentPoint);
        mol.setTemplateAtomAttachmentOrder(amino_idx, amino_idx - 1, kLeftAttachmentPoint);
    }
}

int SequenceLoader::addTemplateAtom(BaseMolecule& mol, const char* alias, const char* monomer_class, int seq_id)
{
    int idx = mol.asMolecule().addAtom(-1);
    mol.asMolecule().setTemplateAtom(idx, alias);
    mol.asMolecule().setTemplateAtomClass(idx, monomer_class);
    mol.asMolecule().setTemplateAtomSeqid(idx, seq_id);
    return idx;
};

void SequenceLoader::addTemplateBond(BaseMolecule& mol, int left_idx, int right_idx, bool branch)
{
    mol.asMolecule().addBond_Silent(left_idx, right_idx, BOND_SINGLE);
    mol.asMolecule().setTemplateAtomAttachmentOrder(left_idx, right_idx, branch ? kBranchAttachmentPoint : kRightAttachmentPoint);
    mol.asMolecule().setTemplateAtomAttachmentOrder(right_idx, left_idx, kLeftAttachmentPoint);
}

Vec3f SequenceLoader::getBackboneMonomerPosition()
{
    return Vec3f(_col * MoleculeLayout::DEFAULT_BOND_LENGTH, -MoleculeLayout::DEFAULT_BOND_LENGTH * _row, 0);
}

void SequenceLoader::addNucleotide(BaseMolecule& mol, std::string base, const std::string& sugar_alias, const std::string& phosphate_alias,
                                   bool phosphate_at_left)
{
    Vec3f pos = getBackboneMonomerPosition();

    // add sugar
    int sugar_idx = addTemplateAtom(mol, sugar_alias.c_str(), kMonomerClassSUGAR, _seq_id);

    mol.asMolecule().setAtomXyz(sugar_idx, pos);

    // add base
    if (base.size() > 0)
    {
        int nuc_base_idx = addTemplateAtom(mol, base.c_str(), kMonomerClassBASE, _seq_id);
        Vec3f base_coord(pos.x, pos.y - MoleculeLayout::DEFAULT_BOND_LENGTH, 0);
        mol.asMolecule().setAtomXyz(nuc_base_idx, base_coord);

        // connect nucleobase to the sugar
        addTemplateBond(mol, sugar_idx, nuc_base_idx, true);
    }

    if (phosphate_alias.size())
    {
        if (phosphate_at_left)
        {
            if (_seq_id > 1)
            {
                // add phosphate
                int phosphate_idx = addTemplateAtom(mol, phosphate_alias.c_str(), kMonomerClassPHOSPHATE, _seq_id - 1);

                Vec3f phosphate_coord(pos.x - MoleculeLayout::DEFAULT_BOND_LENGTH, pos.y, 0);
                mol.asMolecule().setAtomXyz(phosphate_idx, phosphate_coord);

                addTemplateBond(mol, _last_monomer_idx, phosphate_idx); // connect phosphate to the previous monomer
                addTemplateBond(mol, phosphate_idx, sugar_idx);         // connect current sugar to the phosphate
            }
        }
        else // Phosphate at right
        {
            // add phosphate
            int phosphate_idx = addTemplateAtom(mol, phosphate_alias.c_str(), kMonomerClassPHOSPHATE, _seq_id);

            Vec3f phosphate_coord(pos.x + MoleculeLayout::DEFAULT_BOND_LENGTH, pos.y, 0);
            mol.asMolecule().setAtomXyz(phosphate_idx, phosphate_coord);

            if (_last_monomer_idx >= 0)
                addTemplateBond(mol, _last_monomer_idx, sugar_idx); // сonnect sugar to the previous monomer
            addTemplateBond(mol, sugar_idx, phosphate_idx);         // connect phosphate to the current sugar
            _last_monomer_idx = phosphate_idx;
        }
        _col++;
    }
    else if (_last_monomer_idx >= 0)
    {
        // No phosphate - connect sugar to the previous monomer
        addTemplateBond(mol, _last_monomer_idx, sugar_idx);
    }

    if (_last_monomer_idx < 0 || phosphate_at_left)
        _last_monomer_idx = sugar_idx;
}

bool SequenceLoader::addMonomerTemplate(BaseMolecule& mol, MonomerClass mt, const std::string& alias)
{
    int tg_idx = mol.tgroups.addTGroup();
    auto& tg = mol.tgroups.getTGroup(tg_idx);
    if (_mon_lib.getMonomerTemplate(mt, alias, tg))
    {
        tg.tgroup_id = tg_idx;
        return true;
    }
    else
        mol.tgroups.remove(tg_idx);
    return false;
}

// return true if monomer already in templates or successfuly added. otherwise - false
bool SequenceLoader::checkAddTemplate(BaseMolecule& mol, MonomerClass type, const std::string monomer)
{
    if (_added_templates.count(std::make_pair(type, monomer)) == 0)
        if (!addTemplate(mol, monomer, type))
            return false;
    return true;
}

// return true if monomer already in templates or successfuly added. otherwise - false
void SequenceLoader::checkAddTemplate(BaseMolecule& mol, const MonomerTemplate& monomer_template)
{
    if (_added_templates.count(std::make_pair(monomer_template.monomerClass(), monomer_template.alias())) == 0)
    {
        int tg_idx = mol.tgroups.addTGroup();
        auto& tg = mol.tgroups.getTGroup(tg_idx);
        tg.copy(monomer_template.getTGroup());
        tg.tgroup_id = tg_idx;
        _added_templates.emplace(monomer_template.monomerClass(), monomer_template.alias());
    }
}

void SequenceLoader::loadIdt(BaseMolecule& mol)
{
    const auto IDT_DEF_SUGAR = "dR";
    const auto IDT_DEF_PHOSPHATE = "P";
    const auto IDT_MODIFIED_PHOSPHATE = "sP";
    static const std::unordered_set<char> IDT_STANDARD_BASES = {'A', 'T', 'C', 'G', 'U', 'I'};
    _row = 0;
    mol.clear();
    std::string invalid_symbols;
    while (!_scanner.isEOF())
    {
        _seq_id = 0;
        _last_monomer_idx = -1;
        _col = 0;
        std::string sugar = IDT_DEF_SUGAR;
        std::string phosphate = IDT_DEF_PHOSPHATE;
        std::string base = "";
        std::string single_monomer = "";
        std::string single_monomer_class;
        IdtModification modification = IdtModification::FIVE_PRIME_END;

        while (!_scanner.isEOL())
        {
            auto ch = _scanner.readChar();
            switch (ch)
            {
            case '+':
                sugar = "LR";
                break;
            case 'm':
                sugar = "mR";
                break;
            case 'r':
                sugar = "R";
                break;
            case '/':
                // read till next '/'
                ch = 0;
                while (!_scanner.isEOL())
                {
                    ch = _scanner.readChar();
                    if (ch == '/')
                        break;
                    base += ch;
                }
                if (ch != '/')
                    throw Error("Unexpected end of data");
                if (base == "")
                    throw Error("Invalid modification: empty string.");
                if (base.size() < 2)
                    throw Error("Invalid modification: %s.", base.c_str());
                break;
            default:
                base = std::string(1, ch);
                break;
            };

            if (base.size() > 0)
            {
                if (base == "3Phos" && sugar != IDT_DEF_SUGAR)
                    throw Error("Sugar prefix could not be used with '/3Phos/'");
                if (base == "5Phos" && sugar != IDT_DEF_SUGAR)
                    throw Error("Sugar prefix could not be used with '/5Phos/'");
                if (_scanner.isEOL())
                {
                    modification = IdtModification::THREE_PRIME_END;
                    if (base == "3Phos")
                        continue;   // 3phos means that we should not delete phosphate from previuos nucleotide
                    phosphate = ""; // no phosphate at three-prime end
                }
                else if (_scanner.lookNext() == '*')
                {
                    std::ignore = _scanner.readChar();
                    if (_scanner.isEOL())
                        throw Error("Invalid IDT sequence: '*' couldn't be the last symbol.");
                    if (base == "5Phos")
                        throw Error("/5Phos/ cannot be modified to 'sP'");
                    phosphate = IDT_MODIFIED_PHOSPHATE;
                }
                if (base.size() == 1)
                {
                    if (IDT_STANDARD_BASES.count(base[0]) == 0)
                    {
                        if (invalid_symbols.size())
                            invalid_symbols += ',';
                        invalid_symbols += base[0];
                    }
                    else
                    {
                        if (!checkAddTemplate(mol, MonomerClass::Sugar, sugar))
                            throw Error("Unknown sugar '%s'", sugar.c_str());
                        if (base.size() > 0 && !checkAddTemplate(mol, MonomerClass::Base, base))
                            throw Error("Unknown base '%s'", base.c_str());
                        if (phosphate.size() > 0 && !checkAddTemplate(mol, MonomerClass::Phosphate, phosphate))
                            throw Error("Unknown phosphate '%s'", phosphate.c_str());
                    }
                }
                else
                {
                    // Corner case - only one IDT mod - can be 5-end or 3-end - look to first symbol
                    // if (_seq_id == 0 && modification == IdtModification::THREE_PRIME_END && base[0] == '5')
                    //     modification = IdtModification::FIVE_PRIME_END;
                    // Find modification
                    const std::string& mgt_id = MonomerTemplateLibrary::instance().getMGTidByIdtAliasAndMod(base, modification);
                    if (mgt_id.size())
                    {
                        MonomerGroupTemplate& mgt = MonomerTemplateLibrary::instance().getMonomerGroupTemplateById(mgt_id);
                        const MonomerTemplate& sugar_template = mgt.getTemplateByClass(MonomerClass::Sugar);
                        sugar = sugar_template.alias();
                        checkAddTemplate(mol, sugar_template);
                        if (modification != IdtModification::THREE_PRIME_END && mgt.hasTemplateClass(MonomerClass::Phosphate))
                        {
                            if (phosphate == IDT_MODIFIED_PHOSPHATE) // * means that 'sP' should be used
                            {
                                checkAddTemplate(mol, MonomerClass::Phosphate, phosphate);
                            }
                            else // use phosphate from template
                            {
                                const MonomerTemplate& phosphate_template = mgt.getTemplateByClass(MonomerClass::Phosphate);
                                phosphate = phosphate_template.alias();
                                checkAddTemplate(mol, phosphate_template);
                            }
                        }
                        else
                        {
                            phosphate = "";
                        }
                        base = "";
                        if (mgt.hasTemplateClass(MonomerClass::Base))
                        {
                            const MonomerTemplate& base_template = mgt.getTemplateByClass(MonomerClass::Base);
                            base = base_template.alias();
                            checkAddTemplate(mol, base_template);
                        }
                    }
                    else
                    {
                        const std::string& monomer_template_id = MonomerTemplateLibrary::instance().getMonomerTemplateIdByIdtAliasAndMod(base, modification);
                        if (!monomer_template_id.size())
                            throw Error("IDT alias %s not found at %s position.", base.c_str(), IdtAlias::IdtModificationToString(modification).c_str());
                        const MonomerTemplate& monomer_template = MonomerTemplateLibrary::instance().getMonomerTemplateById(monomer_template_id);
                        checkAddTemplate(mol, monomer_template);
                        single_monomer = monomer_template.alias();
                        single_monomer_class = MonomerTemplates::classToStr(monomer_template.monomerClass());
                    }
                }

                if (single_monomer.size())
                {
                    int monomer_idx = addTemplateAtom(mol, single_monomer.c_str(), single_monomer_class.c_str(), _seq_id);
                    mol.asMolecule().setAtomXyz(monomer_idx, getBackboneMonomerPosition());
                    if (_last_monomer_idx >= 0)
                        addTemplateBond(mol, _last_monomer_idx, monomer_idx);
                    _last_monomer_idx = monomer_idx;
                }
                else
                    addNucleotide(mol, base, sugar, phosphate, false);

                _seq_id++;
                _col++;

                sugar = IDT_DEF_SUGAR; // reset sugar to default
                phosphate = IDT_DEF_PHOSPHATE;
                base = "";
                single_monomer = "";
                modification = IdtModification::INTERNAL;
            }
        }
        while (!_scanner.isEOF() && _scanner.isEOL()) // Skip EOL characters
            _scanner.skip(1);
        _row += 2;
    }

    if (invalid_symbols.size())
        throw Error("SequenceLoader::loadIdt(), Invalid symbols in the sequence: %s", invalid_symbols.c_str());
}
