// Copyright 2018 Google LLC. All Rights Reserved.
/*
  Copyright (C) 2005 Steven L. Scott

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/

#include "Models/DataTypes.hpp"
#include <sstream>
#include <stdexcept>
#include <vector>

namespace BOOM {

  ostream &operator<<(ostream &out, const Ptr<Data> &dp) {
    return dp->display(out);
  }

  ostream &operator<<(ostream &out, const Data &d) {
    d.display(out);
    return out;
  }

  void print_data(const Data &d) { std::cout << d << std::endl; }

  void intrusive_ptr_add_ref(Data *d) { d->up_count(); }
  void intrusive_ptr_release(Data *d) {
    d->down_count();
    if (d->ref_count() == 0) {
      delete d;
    }
  }

  Data::missing_status Data::missing() const { return missing_flag; }
  void Data::set_missing_status(missing_status m) { missing_flag = m; }

  //------------------------------------------------------------
  VectorData::VectorData(uint n, double X) : x(n, X) {}
  VectorData::VectorData(const Vector &y) : x(y) {}
  VectorData::VectorData(const VectorData &rhs)
      : Data(rhs), Traits(rhs), x(rhs.x) {}
  VectorData *VectorData::clone() const { return new VectorData(*this); }

  ostream &VectorData::display(ostream &out) const {
    out << x;
    return out;
  }

  void VectorData::set(const Vector &rhs, bool sig) {
    x = rhs;
    if (sig) {
      signal();
    }
  }

  void VectorData::set_element(double value, int position, bool sig) {
    x[position] = value;
    if (sig) {
      signal();
    }
  }

  double VectorData::operator[](uint i) const { return x[i]; }

  double &VectorData::operator[](uint i) {
    signal();
    return x[i];
  }

  //------------------------------------------------------------
  MatrixData::MatrixData(int r, int c, double val) : x(r, c, val) {}

  MatrixData::MatrixData(const Matrix &y) : x(y) {}

  MatrixData::MatrixData(const MatrixData &rhs)
      : Data(rhs), Traits(rhs), x(rhs.x) {}

  MatrixData *MatrixData::clone() const { return new MatrixData(*this); }
  ostream &MatrixData::display(ostream &out) const {
    out << x << endl;
    return out;
  }

  void MatrixData::set(const Matrix &rhs, bool sig) {
    x = rhs;
    if (sig) {
      signal();
    }
  }

  void MatrixData::set_element(double value, int row, int col, bool sig) {
    x(row, col) = value;
    if (sig) {
      signal();
    }
  }

}  // namespace BOOM
